#include "rendering/metal/MetalSplatRenderer.h"

#if SPLATKIT_HAS_METALFX
// Recorded in the object as an autolink option, so a static consumer (SwiftPM, CocoaPods)
// links MetalFX exactly where this slice was built against it.
asm(".linker_option \"-framework\", \"MetalFX\"");
#endif

#include <TargetConditionals.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

#include "rendering/metal/device/MetalShaderLibrary.h"
#include "rendering/metal/layout/MetalShaderTypes.h"
#include "splat/math/Mat4.h"
#include "splatkit/Log.h"

namespace splatkit {

namespace {
constexpr MTLClearColor kBackground = {0.05, 0.05, 0.08, 1.0};
}  // namespace

std::unique_ptr<MetalSplatRenderer> MetalSplatRenderer::create() {
  std::unique_ptr<MetalSplatRenderer> r(new MetalSplatRenderer());
  r->context_ = MetalContext::create();
  if (r->context_ == nullptr) return nullptr;
  for (auto& uniform : r->uniforms_) {
    uniform = metal::buffer(r->context_->device(), sizeof(CameraUniform));
    if (uniform == nil) return nullptr;
  }
  r->target_ = std::make_unique<MetalRenderTarget>(r->context_->device());
  r->depthBits_ = MetalRadixSort::KeyBits::Full32;
  r->gpuSort_ = r->visibility_.create(r->context_->device(), r->context_->library(), r->depthBits_);
  LOGI("Metal depth keys: %u bits, %u radix passes (uint32 scratch)",
       static_cast<uint32_t>(r->depthBits_), static_cast<uint32_t>(r->depthBits_) / 8);
  if (!r->gpuSort_) LOGW("GPU sort unavailable, sorting on the CPU");
  r->inFlight_ = dispatch_semaphore_create(kFramesInFlight);
  LOGI("%s", r->context_->description().c_str());
  return r;
}

MetalSplatRenderer::~MetalSplatRenderer() {
  waitIdle();
}

// Waits for every frame in flight: the order and world buffers may be released after.
void MetalSplatRenderer::waitIdle() {
  if (inFlight_ == nullptr) return;
  for (uint32_t i = 0; i < kFramesInFlight; ++i) {
    dispatch_semaphore_wait(inFlight_, DISPATCH_TIME_FOREVER);
  }
  for (uint32_t i = 0; i < kFramesInFlight; ++i) dispatch_semaphore_signal(inFlight_);
}

void MetalSplatRenderer::setLayer(CAMetalLayer* layer) {
  if (layer == layer_) return;
  waitIdle();
  layer_ = layer;
  if (layer_ == nil) return;
  layer_.device = context_->device();
  layer_.pixelFormat = pixelFormat();
  layer_.framebufferOnly = YES;
  ++generation_;
  if (pipelineFormat_ != targetFormat() && !createPipelines()) {
    LOGE("rendering stopped: no pipelines");
    layer_ = nil;
  }
}

void MetalSplatRenderer::setDrawableSize(uint32_t width, uint32_t height) {
  if (width == width_ && height == height_) return;
  LOGI("drawable %ux%u, was %ux%u", width, height, width_, height_);
  width_ = width;
  height_ = height;
  if (scaler_ != nil) waitIdle();
  ++generation_;
  createTarget();
}

void MetalSplatRenderer::setRenderScale(float scale) {
  scale = std::clamp(scale, 0.1f, 2.0f);
  if (scale == renderScale_) return;
  renderScale_ = scale;
  if (scaler_ != nil) waitIdle();
  ++generation_;
  createTarget();
}

void MetalSplatRenderer::setSpatialUpscaling(bool enabled) {
  if (enabled == spatialUpscaling_) return;
  spatialUpscaling_ = enabled;
  // The scaler is not retained by the command buffers that encode it, so it must not be
  // replaced while one is in flight.
  if (scaler_ != nil) waitIdle();
  ++generation_;
  createTarget();
}

void MetalSplatRenderer::setLinearBlending(bool linear) {
  if (linear == linearBlending_) return;
  linearBlending_ = linear;
  waitIdle();
  if (layer_ != nil) layer_.pixelFormat = pixelFormat();
  ++generation_;
  createTarget();
  if (!createPipelines()) {
    LOGE("rendering stopped: no pipelines");
    layer_ = nil;
  }
}

void MetalSplatRenderer::setVsync(bool vsync) {
  // Presentation is tied to the display link that drives the frames; a benchmark
  // without vsync would need its own loop. Host frame intervals and GPU times differ.
  (void)vsync;
}

DeviceCapabilities MetalSplatRenderer::deviceCapabilities() const {
  DeviceCapabilities caps;
  // MetalLodSelection caps a selected cut at 4M nodes; the engine's residency window is
  // [100k, 32M].
  caps.limits.maxLodCapacitySplats = MetalLodSelection::kMaxBudget;
  caps.limits.minResidencyCapacitySplats = 100'000;
  caps.limits.maxResidencyCapacitySplats = 32'000'000;
  caps.supportsComputeTiles = false;
  caps.supportsHiZOcclusion = false;
  caps.supportsSubgroups = gpuSort_;
  // Metal exposes no texture-dimension query; Apple family 7, the minimum this renderer
  // accepts, supports 16384x16384.
  caps.maxTextureDimension = context_->device() != nil ? 16384u : 0u;
  RenderPolicySupport& policy = caps.policy;
  policy.fallback.raster = RasterStrategy::hardware;
  policy.fallback.tileSize = 16;
  policy.fallback.lodErrorPixels = 1.0f;
  policy.fallback.alphaThreshold = kQuadMinAlpha;
  policy.fallback.subpixelThreshold = kMinPixelRadius;
  policy.fallback.sortDepth =
      depthBits_ == MetalRadixSort::KeyBits::Low16 ? SortKeyBits::low16 : SortKeyBits::full32;
  policy.fallback.enableFrustumCulling = true;
  policy.fallback.enableEarlyTermination = true;
  // The key width is a public per-instance control. The raster strategy, sub-pixel radius,
  // alpha, tile size and occlusion stay fixed shader/rasterization choices.
  // The LOD error threshold applies live to a hierarchy world and at its next upload.
  policy.lodErrorPixels = gpuSort_;
  policy.minLodErrorPixels = 0.1f;
  policy.maxLodErrorPixels = 16.0f;
  policy.lodSplatLimit = gpuSort_;
  policy.sortDepth = gpuSort_;
  return caps;
}

bool MetalSplatRenderer::applyRenderPolicy(const RenderPolicy& policy, std::string* reason) {
  const MetalRadixSort::KeyBits bits = policy.sortDepth == SortKeyBits::low16
                                           ? MetalRadixSort::KeyBits::Low16
                                           : MetalRadixSort::KeyBits::Full32;
  const bool visibilityChanged = bits != depthBits_;
  const bool lodChanged =
      policy.lodErrorPixels != lodErrorPixels_ || policy.lodSplatLimit != lodSplatLimit_;
  if (!visibilityChanged && !lodChanged) return true;
  if (!gpuSort_) {
    // No GPU ordering pipelines exist; nothing the policy can reach.
    if (reason != nullptr) *reason = "Metal GPU ordering is unavailable";
    return false;
  }
  // Rebuilding pipelines or the target while frames may still encode against them is unsafe.
  waitIdle();
  if (visibilityChanged && !visibility_.reconfigure(bits)) {
    if (reason != nullptr) *reason = "Metal visibility pipeline rebuild failed";
    return false;
  }
  depthBits_ = bits;
  // Plain configuration read by the next selection encode; it cannot fail, so it goes last.
  lodErrorPixels_ = policy.lodErrorPixels;
  lodSplatLimit_ = policy.lodSplatLimit;
  lodPixels_ = lodErrorPixels_;
  // Frames are idle here, so every readback so far describes the previous policy.
  lodAdaptedReadback_ = lodReadbacks_.load();
  if (lod_) {
    lod_->setPixelLimit(lodPixels_);
    lod_->setSplatLimit(lodSplatLimit_);
  }
  LOGI("Metal policy: %u-bit sort keys, %.2fpx LOD error, %u LOD splat limit",
       static_cast<uint32_t>(bits), lodErrorPixels_, lodSplatLimit_);
  return true;
}

MTLPixelFormat MetalSplatRenderer::pixelFormat() const {
  return linearBlending_ ? MTLPixelFormatBGRA8Unorm_sRGB : MTLPixelFormatBGRA8Unorm;
}

Extent MetalSplatRenderer::drawExtent() const {
  if (id<MTLTexture> color = target_->color()) {
    return {static_cast<uint32_t>(color.width), static_cast<uint32_t>(color.height)};
  }
  return {width_, height_};
}

MTLPixelFormat MetalSplatRenderer::targetFormat() const {
  return gpuSort_ ? MTLPixelFormatRGBA16Float : pixelFormat();
}

bool MetalSplatRenderer::createTarget() {
  scaler_ = nil;
  upscaled_ = nil;
  if ((renderScale_ == 1.0f && !gpuSort_) || width_ == 0 || height_ == 0) {
    target_->release();
    return true;
  }
  if (!target_->resize(width_, height_, renderScale_, targetFormat())) return false;
  return createScaler();
}

// MetalFX requires a private-storage output, which the iOS drawable is not, so the scaler
// writes upscaled_ at drawable size and the blit pass copies that to the drawable.
bool MetalSplatRenderer::createScaler() {
  scaler_ = nil;
  upscaled_ = nil;
  const bool wanted = spatialUpscaling_ && target_->color() != nil && renderScale_ < 1.0f;
#if SPLATKIT_HAS_METALFX
  if (wanted) {
    if (![MTLFXSpatialScalerDescriptor supportsDevice:context_->device()]) {
      LOGI("MetalFX spatial scaler unsupported here; the scaled frame is blitted");
    } else {
      MTLFXSpatialScalerDescriptor* desc = [MTLFXSpatialScalerDescriptor new];
      id<MTLTexture> color = target_->color();
      desc.inputWidth = color.width;
      desc.inputHeight = color.height;
      desc.outputWidth = width_;
      desc.outputHeight = height_;
      desc.colorTextureFormat = color.pixelFormat;
      desc.outputTextureFormat = pixelFormat();
      desc.colorProcessingMode = linearBlending_ ? MTLFXSpatialScalerColorProcessingModeLinear
                                                 : MTLFXSpatialScalerColorProcessingModePerceptual;
      scaler_ = [desc newSpatialScalerWithDevice:context_->device()];
      if (scaler_ == nil) {
        LOGE("MetalFX spatial scaler %lux%lu to %ux%u failed; the scaled frame is blitted",
             static_cast<unsigned long>(color.width), static_cast<unsigned long>(color.height),
             width_, height_);
      } else {
        scaler_.inputContentWidth = color.width;
        scaler_.inputContentHeight = color.height;
        MTLTextureDescriptor* out =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pixelFormat()
                                                               width:width_
                                                              height:height_
                                                           mipmapped:NO];
        out.usage =
            MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite | MTLTextureUsageRenderTarget;
        out.storageMode = MTLStorageModePrivate;
        upscaled_ = [context_->device() newTextureWithDescriptor:out];
        if (upscaled_ == nil) {
          LOGE("MetalFX output %ux%u failed; the scaled frame is blitted", width_, height_);
          scaler_ = nil;
          return true;
        }
        LOGI("MetalFX spatial scaler %lux%lu to %ux%u", static_cast<unsigned long>(color.width),
             static_cast<unsigned long>(color.height), width_, height_);
      }
    }
  }
#else
  if (wanted) LOGI("MetalFX is absent from this SDK; the scaled frame is blitted");
#endif
  return true;
}

// One splat pipeline per spherical harmonics degree, specialised through a function
// constant so a degree 0 world pays nothing for SH, plus the render scale pass.
bool MetalSplatRenderer::createPipelines() {
  pipelineFormat_ = targetFormat();
  NSError* error = nil;
  id<MTLFunction> fragment = [context_->library() newFunctionWithName:@"splatFragment"];
  for (int degree = 0; degree <= kMaxShDegree; ++degree) {
    MTLFunctionConstantValues* constants = [MTLFunctionConstantValues new];
    uint32_t value = static_cast<uint32_t>(degree);
    [constants setConstantValue:&value type:MTLDataTypeUInt atIndex:kFnShDegree];
    id<MTLFunction> vertex = [context_->library() newFunctionWithName:@"splatVertex"
                                                       constantValues:constants
                                                                error:&error];
    if (vertex == nil) {
      LOGE("splat vertex function: %s", error.localizedDescription.UTF8String);
      return false;
    }
    MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
    desc.vertexFunction = vertex;
    desc.fragmentFunction = fragment;
    desc.colorAttachments[0].pixelFormat = pipelineFormat_;
    desc.depthAttachmentPixelFormat = MetalRenderTarget::kDepthFormat;
    // "Over" compositing, back to front: out = src.a * src + (1 - src.a) * dst.
    desc.colorAttachments[0].blendingEnabled = YES;
    desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    id<MTLRenderPipelineState> state =
        [context_->device() newRenderPipelineStateWithDescriptor:desc error:&error];
    if (state == nil) {
      LOGE("splat pipeline degree %d: %s", degree, error.localizedDescription.UTF8String);
      return false;
    }
    splatPipelines_[static_cast<size_t>(degree)] = state;
  }

  // The GPU order path draws the projections the visibility kernel wrote, front to
  // back with "under" compositing of premultiplied colour onto a clear of zero:
  // out = (1 - dst.a) * src + dst, for colour and coverage alike.
  MTLRenderPipelineDescriptor* under = [MTLRenderPipelineDescriptor new];
  under.vertexFunction = [context_->library() newFunctionWithName:@"projectedVertex"];
  under.fragmentFunction = [context_->library() newFunctionWithName:@"splatFragmentUnder"];
  under.colorAttachments[0].pixelFormat = pipelineFormat_;
  under.depthAttachmentPixelFormat = MetalRenderTarget::kDepthFormat;
  under.colorAttachments[0].blendingEnabled = YES;
  under.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOneMinusDestinationAlpha;
  under.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOne;
  under.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOneMinusDestinationAlpha;
  under.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
  projectedPipeline_ = [context_->device() newRenderPipelineStateWithDescriptor:under error:&error];
  if (projectedPipeline_ == nil) {
    LOGE("projected pipeline: %s", error.localizedDescription.UTF8String);
    return false;
  }
  // The background goes under whatever coverage is left, with the same blend.
  under.vertexFunction = [context_->library() newFunctionWithName:@"blitVertex"];
  under.fragmentFunction = [context_->library() newFunctionWithName:@"backgroundFragment"];
  backgroundPipeline_ = [context_->device() newRenderPipelineStateWithDescriptor:under
                                                                           error:&error];
  if (backgroundPipeline_ == nil) {
    LOGE("background pipeline: %s", error.localizedDescription.UTF8String);
    return false;
  }
  // The saturation mask touches the depth buffer only.
  MTLRenderPipelineDescriptor* mask = [MTLRenderPipelineDescriptor new];
  mask.vertexFunction = [context_->library() newFunctionWithName:@"blitVertex"];
  mask.fragmentFunction = [context_->library() newFunctionWithName:@"saturationMask"];
  mask.colorAttachments[0].pixelFormat = pipelineFormat_;
  mask.colorAttachments[0].writeMask = MTLColorWriteMaskNone;
  mask.depthAttachmentPixelFormat = MetalRenderTarget::kDepthFormat;
  maskPipeline_ = [context_->device() newRenderPipelineStateWithDescriptor:mask error:&error];
  if (maskPipeline_ == nil) {
    LOGE("saturation mask pipeline: %s", error.localizedDescription.UTF8String);
    return false;
  }
  MTLDepthStencilDescriptor* depth = [MTLDepthStencilDescriptor new];
  depth.depthCompareFunction = MTLCompareFunctionLessEqual;
  depth.depthWriteEnabled = NO;
  splatDepth_ = [context_->device() newDepthStencilStateWithDescriptor:depth];
  depth.depthCompareFunction = MTLCompareFunctionAlways;
  depth.depthWriteEnabled = YES;
  maskDepth_ = [context_->device() newDepthStencilStateWithDescriptor:depth];

  MTLRenderPipelineDescriptor* blit = [MTLRenderPipelineDescriptor new];
  blit.vertexFunction = [context_->library() newFunctionWithName:@"blitVertex"];
  blit.fragmentFunction = [context_->library() newFunctionWithName:@"blitFragment"];
  blit.colorAttachments[0].pixelFormat = pixelFormat();
  blitPipeline_ = [context_->device() newRenderPipelineStateWithDescriptor:blit error:&error];
  if (blitPipeline_ == nil) {
    LOGE("blit pipeline: %s", error.localizedDescription.UTF8String);
    return false;
  }
  LOGI("pipelines for format %lu", static_cast<unsigned long>(pipelineFormat_));
  return true;
}

std::optional<std::uint64_t> MetalSplatRenderer::availableMemory() const {
  return context_->availableMemory();
}

// Worlds.

bool MetalSplatRenderer::uploadWorld(const splat::SplatCloud& cloud, int maxShDegree) {
  auto world = MetalWorld::upload(context_->device(), cloud, maxShDegree);
  if (!world) return false;
  waitIdle();
  if (gpuSort_ && !visibility_.reserve(world->info().count)) return false;
  world_ = std::move(world);
  lod_.reset();
  completedWorldFrame_.store(false);
  lastSelectedCount_.store(0);
  lastSelectMillis_.store(0);
  return true;
}

bool MetalSplatRenderer::uploadLodWorld(const splat::LodTree& tree, int maxShDegree,
                                        uint32_t budget) {
  if (!gpuSort_) return false;
  waitIdle();
  auto lod = std::make_unique<MetalLodSelection>();
  if (!lod->create(context_->device(), context_->library()) ||
      !lod->upload(tree, budget, lodErrorPixels_))
    return false;
  lod->setSplatLimit(lodSplatLimit_);
  // Compact projections and radix scratch scale with the cut, not all resident nodes.
  MetalVisibilityPass visibility;
  if (!visibility.create(context_->device(), context_->library(), depthBits_) ||
      !visibility.reserve(static_cast<uint32_t>(tree.nodeCount()), lod->budget()))
    return false;
  std::array<id<MTLBuffer>, kFramesInFlight> readback{};
  for (auto& buffer : readback) {
    buffer = metal::buffer(context_->device(), 6 * sizeof(uint32_t));
    if (!buffer) return false;
  }
  auto world = MetalWorld::upload(context_->device(), tree.nodes, maxShDegree);
  if (!world) return false;
  visibility_ = std::move(visibility);
  lod_ = std::move(lod);
  lodReadback_ = readback;
  lodPixels_ = lodErrorPixels_;
  lodAdaptedReadback_ = lodReadbacks_.load();
  lastLodLimitedCount_.store(0);
  lastLodEvaluatedCount_.store(0);
  world_ = std::move(world);
  completedWorldFrame_.store(false);
  lastSelectedCount_.store(0);
  lastSelectMillis_.store(0);
  return true;
}

bool MetalSplatRenderer::createSlab(uint32_t capacity, int shDegree) {
  auto world = MetalWorld::slab(context_->device(), capacity, shDegree);
  if (!world) return false;
  waitIdle();
  if (gpuSort_ && !visibility_.reserve(capacity)) return false;
  world_ = std::move(world);
  lod_.reset();
  completedWorldFrame_.store(false);
  lastSelectedCount_.store(0);
  lastSelectMillis_.store(0);
  return true;
}

bool MetalSplatRenderer::uploadTile(uint32_t offset, const splat::SplatCloud& cloud) {
  return world_ && world_->uploadTile(offset, cloud);
}

std::optional<GpuWorldInfo> MetalSplatRenderer::world() const {
  return world_ ? std::optional<GpuWorldInfo>{world_->info()} : std::nullopt;
}

// The frame.

// Selection counts arrive a frame or two late, so the threshold climbs fast while refinements
// are denied and relaxes slowly, holding inside a 10% band below the limit.
void MetalSplatRenderer::adaptLodThreshold() {
  constexpr float kRaise = 1.1f;
  constexpr float kRelax = 1.03f;
  constexpr float kMaxPixels = 64.0f;
  const uint32_t readbacks = lodReadbacks_.load();
  if (readbacks == lodAdaptedReadback_) return;
  lodAdaptedReadback_ = readbacks;
  if (lastLodLimitedCount_.load() > 0) {
    lodPixels_ = std::min(lodPixels_ * kRaise, std::max(kMaxPixels, lodErrorPixels_));
  } else if (lastSelectedCount_.load() < lod_->limit() / 10 * 9) {
    lodPixels_ = std::max(lodPixels_ / kRelax, lodErrorPixels_);
  }
  lod_->setPixelLimit(lodPixels_);
}

uint32_t MetalSplatRenderer::takePresentTimes(std::vector<int64_t>* times) {
  times->clear();
  const std::lock_guard<std::mutex> lock(presents_->mutex);
  times->swap(presents_->times);
  return std::exchange(presents_->dropped, 0);
}

bool MetalSplatRenderer::draw(const Frame& frame) {
  if (gpuFailed_.load()) return false;
  if (!ready() || splatPipelines_[0] == nil) return false;
  dispatch_semaphore_wait(inFlight_, DISPATCH_TIME_FOREVER);
  // Until submitFrame(), every failure returns the acquired slot synchronously.
  if (gpuFailed_.load()) {
    dispatch_semaphore_signal(inFlight_);
    return false;
  }
  // Capture requires a readable drawable before nextDrawable() acquires it.
  if (capture_) layer_.framebufferOnly = NO;
  id<CAMetalDrawable> drawable = [layer_ nextDrawable];
  if (drawable == nil) {
    dispatch_semaphore_signal(inFlight_);
    return false;
  }
  const uint32_t slot = static_cast<uint32_t>(frame_ % kFramesInFlight);

  // A new order goes into the buffer the frame in flight is not reading.
  uint32_t drawCount = 0;
  if (world_) {
    if (frame.order != nullptr && !world_->writeOrder(frame.order, frame.orderCount)) {
      dispatch_semaphore_signal(inFlight_);
      return false;
    }
    drawCount = std::min(frame.drawCount, world_->info().count);
  }
  updateCameraUniforms(frame, slot);

  const bool gpuOrder = world_ && frame.orderSource == OrderSource::gpu && gpuSort_;
  id<MTLCommandBuffer> selection = gpuOrder && lod_ ? encodeSelection(slot) : nil;
  id<MTLCommandBuffer> sort = gpuOrder ? encodeVisibilityAndSort(frame, slot) : nil;
  if (gpuOrder && sort == nil) {
    // Selection has only been encoded, so no GPU work can still use this slot.
    dispatch_semaphore_signal(inFlight_);
    return false;
  }

  id<MTLCommandBuffer> cmd = [context_->queue() commandBuffer];
  encodeRaster(cmd, drawable.texture, frame, slot, drawCount, gpuOrder);
  encodeOutput(cmd, drawable.texture);
  CaptureHandler onCapture;
  id<MTLBuffer> captured = encodeCapture(cmd, drawable.texture, &onCapture);

  // Submit dependencies in order on the same queue. The final completion releases
  // the slot only after selection, visibility/sort and rendering have finished.
  if (selection != nil) [selection commit];
  if (sort != nil) [sort commit];
  const bool drewWorld = world_ && (gpuOrder || drawCount > 0);
  submitFrame(cmd, drawable, drewWorld, captured, std::move(onCapture));
  ++frame_;
  return true;
}

void MetalSplatRenderer::updateCameraUniforms(const Frame& frame, uint32_t slot) {
  const Extent extent = drawExtent();
  CameraUniform u{};
  u.view = frame.view;
  u.proj = frame.proj;
  u.screenSize[0] = static_cast<float>(extent.width);
  u.screenSize[1] = static_cast<float>(extent.height);
  u.focal[0] = u.screenSize[0] * frame.proj.at(0, 0) / 2;
  u.focal[1] = u.screenSize[1] * frame.proj.at(1, 1) / 2;
  u.tanHalfFov[0] = 1 / frame.proj.at(0, 0);
  u.tanHalfFov[1] = 1 / frame.proj.at(1, 1);
  u.outputLinear = linearBlending_ ? 1u : 0u;
  u.cameraPosition[0] = frame.cameraPosition.x;
  u.cameraPosition[1] = frame.cameraPosition.y;
  u.cameraPosition[2] = frame.cameraPosition.z;
  std::memcpy(uniforms_[slot].contents, &u, sizeof(u));
}

id<MTLCommandBuffer> MetalSplatRenderer::encodeSelection(uint32_t slot) {
  adaptLodThreshold();
  auto selection = [context_->queue() commandBuffer];
  selection.label = @"GPU LOD selection";
  lod_->encode(selection, uniforms_[slot]);
  id<MTLBuffer> selectedCount = lodReadback_[slot];
  auto readback = [selection blitCommandEncoder];
  [readback copyFromBuffer:lod_->count()
              sourceOffset:0
                  toBuffer:selectedCount
         destinationOffset:0
                      size:24];
  [readback endEncoding];
  std::atomic<uint32_t>* selected = &lastSelectedCount_;
  std::atomic<double>* milliseconds = &lastSelectMillis_;
  std::atomic<uint32_t>* limited = &lastLodLimitedCount_;
  std::atomic<uint32_t>* evaluated = &lastLodEvaluatedCount_;
  const bool logLod = frame_ % 120 == 0;
  std::atomic<uint32_t>* readbacks = &lodReadbacks_;
  const float pixels = lodPixels_;
  std::atomic<bool>* failed = &gpuFailed_;
  [selection addCompletedHandler:^(id<MTLCommandBuffer> done) {
    if (done.status == MTLCommandBufferStatusError) {
      failed->store(true);
      LOGE("LOD command failed: %s", done.error.localizedDescription.UTF8String);
      return;
    }
    selected->store(*static_cast<const uint32_t*>(selectedCount.contents));
    const auto* counters = static_cast<const uint32_t*>(selectedCount.contents);
    limited->store(counters[4]);
    evaluated->store(counters[5]);
    readbacks->fetch_add(1);
    if (logLod)
      LOGI("LOD SSE: %u selected, %u evaluated interiors, %u quality-limited refinements, "
           "%.2f px",
           counters[0], counters[5], counters[4], pixels);
    milliseconds->store((done.GPUEndTime - done.GPUStartTime) * 1000.0);
  }];
  return selection;
}

id<MTLCommandBuffer> MetalSplatRenderer::encodeVisibilityAndSort(const Frame& frame,
                                                                 uint32_t slot) {
  id<MTLCommandBuffer> sort = [context_->queue() commandBuffer];
  const int degree = std::clamp(std::min(frame.shDegree, world_->info().shDegree), 0, kMaxShDegree);
  if (!visibility_.encode(sort, slot, uniforms_[slot], world_->splats(), world_->harmonics(),
                          degree, lod_ ? nullptr : frame.ranges, lod_ ? 0 : frame.rangeCount,
                          lod_ ? lod_->indices() : nil, lod_ ? lod_->count() : nil)) {
    LOGE("invalid visibility ranges");
    return nil;
  }
  std::atomic<double>* sortMillis = &lastSortMillis_;
  std::atomic<uint32_t>* drawn = &lastDrawCount_;
  id<MTLBuffer> countBuffer = visibility_.countBuffer(slot);
  std::atomic<bool>* failed = &gpuFailed_;
  [sort addCompletedHandler:^(id<MTLCommandBuffer> done) {
    if (done.status == MTLCommandBufferStatusError) {
      failed->store(true);
      LOGE("visibility command failed: %s", done.error.localizedDescription.UTF8String);
      return;
    }
    sortMillis->store((done.GPUEndTime - done.GPUStartTime) * 1000.0);
    drawn->store(*static_cast<const uint32_t*>(countBuffer.contents));
  }];
  return sort;
}

void MetalSplatRenderer::encodeRaster(id<MTLCommandBuffer> cmd, id<MTLTexture> drawableTexture,
                                      const Frame& frame, uint32_t slot, uint32_t drawCount,
                                      bool gpuOrder) {
  MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
  id<MTLTexture> colour = target_->color() != nil ? target_->color() : drawableTexture;
  pass.colorAttachments[0].texture = colour;
  pass.colorAttachments[0].loadAction = MTLLoadActionClear;
  pass.colorAttachments[0].storeAction = MTLStoreActionStore;
  // Front to back accumulates onto nothing and puts the background under at the end.
  pass.colorAttachments[0].clearColor = gpuOrder ? MTLClearColorMake(0, 0, 0, 0) : kBackground;
  if (target_->ensureDepth(colour.width, colour.height)) {
    pass.depthAttachment.texture = target_->depth();
    pass.depthAttachment.loadAction = MTLLoadActionClear;
    pass.depthAttachment.storeAction = MTLStoreActionDontCare;
    pass.depthAttachment.clearDepth = 1.0;
  }
  id<MTLRenderCommandEncoder> encoder = [cmd renderCommandEncoderWithDescriptor:pass];
  if (world_ && (drawCount > 0 || gpuOrder)) {
    [encoder setVertexBuffer:uniforms_[slot] offset:0 atIndex:0];
    if (gpuOrder) {
      [encoder setVertexBuffer:visibility_.projected() offset:0 atIndex:1];
      [encoder setVertexBuffer:visibility_.order() offset:0 atIndex:2];
      id<MTLBuffer> arguments = visibility_.drawArguments(slot);
      for (uint32_t batch = 0; batch < MetalVisibilityPass::kDrawBatches; ++batch) {
        if (batch > 0) {
          [encoder setRenderPipelineState:maskPipeline_];
          [encoder setDepthStencilState:maskDepth_];
          [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        }
        [encoder setRenderPipelineState:projectedPipeline_];
        [encoder setDepthStencilState:splatDepth_];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                  indirectBuffer:arguments
            indirectBufferOffset:batch * MetalVisibilityPass::kDrawArgumentBytes];
      }
    } else {
      const int degree =
          std::clamp(std::min(frame.shDegree, world_->info().shDegree), 0, kMaxShDegree);
      [encoder setRenderPipelineState:splatPipelines_[static_cast<size_t>(degree)]];
      [encoder setDepthStencilState:splatDepth_];
      [encoder setVertexBuffer:world_->splats() offset:0 atIndex:1];
      [encoder setVertexBuffer:world_->harmonics() offset:0 atIndex:3];
      [encoder setVertexBuffer:world_->order() offset:0 atIndex:2];
      [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                  vertexStart:0
                  vertexCount:4
                instanceCount:drawCount];
    }
  }
  if (gpuOrder) {
    const float background[4] = {static_cast<float>(kBackground.red),
                                 static_cast<float>(kBackground.green),
                                 static_cast<float>(kBackground.blue), 1.0f};
    [encoder setRenderPipelineState:backgroundPipeline_];
    [encoder setDepthStencilState:splatDepth_];
    [encoder setFragmentBytes:background length:sizeof(background) atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
  }
  [encoder endEncoding];
}

void MetalSplatRenderer::encodeOutput(id<MTLCommandBuffer> cmd, id<MTLTexture> drawableTexture) {
  id<MTLTexture> source = target_->color();
#if SPLATKIT_HAS_METALFX
  if (scaler_ != nil) {
    scaler_.colorTexture = source;
    scaler_.outputTexture = upscaled_;
    [scaler_ encodeToCommandBuffer:cmd];
    source = upscaled_;
  }
#endif
  if (source != nil) {
    MTLRenderPassDescriptor* blit = [MTLRenderPassDescriptor renderPassDescriptor];
    blit.colorAttachments[0].texture = drawableTexture;
    blit.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    blit.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> scale = [cmd renderCommandEncoderWithDescriptor:blit];
    [scale setRenderPipelineState:blitPipeline_];
    [scale setFragmentTexture:source atIndex:0];
    [scale drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [scale endEncoding];
  }
}

id<MTLBuffer> MetalSplatRenderer::encodeCapture(id<MTLCommandBuffer> cmd,
                                                id<MTLTexture> drawableTexture,
                                                CaptureHandler* onCapture) {
  id<MTLBuffer> captured = nil;
  if (capture_) {
    if (drawableTexture.framebufferOnly) {
      LOGW("capture skipped: the drawable is not readable yet");
    } else {
      const NSUInteger bytesPerRow = NSUInteger{width_} * 4;
      captured = [context_->device() newBufferWithLength:bytesPerRow * height_
                                                 options:MTLResourceStorageModeShared];
      id<MTLBlitCommandEncoder> copy = [cmd blitCommandEncoder];
      [copy copyFromTexture:drawableTexture
                       sourceSlice:0
                       sourceLevel:0
                      sourceOrigin:MTLOriginMake(0, 0, 0)
                        sourceSize:MTLSizeMake(width_, height_, 1)
                          toBuffer:captured
                 destinationOffset:0
            destinationBytesPerRow:bytesPerRow
          destinationBytesPerImage:bytesPerRow * height_];
      [copy endEncoding];
      *onCapture = std::move(capture_);
      capture_ = nullptr;
      layer_.framebufferOnly = YES;
    }
  }

  return captured;
}

void MetalSplatRenderer::submitFrame(id<MTLCommandBuffer> cmd, id<CAMetalDrawable> drawable,
                                     bool drewWorld, id<MTLBuffer> captured,
                                     CaptureHandler onCapture) {
  // presentedTime is when the frame reached the display, zero when it was never shown.
  // The simulator's Metal has no presentation handler, so it reports no present timing.
#if !TARGET_OS_SIMULATOR
  std::shared_ptr<PresentLog> presents = presents_;
  [drawable addPresentedHandler:^(id<MTLDrawable> shown) {
    const CFTimeInterval time = shown.presentedTime;
    const std::lock_guard<std::mutex> lock(presents->mutex);
    if (time <= 0) {
      ++presents->dropped;
    } else if (presents->times.size() < 1024) {
      presents->times.push_back(static_cast<int64_t>(time * 1e9));
    }
  }];
#endif
  [cmd presentDrawable:drawable];
  dispatch_semaphore_t inFlight = inFlight_;
  std::atomic<double>* gpuMillis = &lastGpuMillis_;
  std::atomic<bool>* failed = &gpuFailed_;
  const uint32_t width = width_;
  const uint32_t height = height_;
  auto* worldFrame = &completedWorldFrame_;
  [cmd addCompletedHandler:^(id<MTLCommandBuffer> done) {
    if (done.status == MTLCommandBufferStatusError) {
      failed->store(true);
      LOGE("render command failed: %s", done.error.localizedDescription.UTF8String);
      // Failed timestamps/capture bytes do not describe a presented frame.
      gpuMillis->store(0.0);
      if (onCapture) onCapture({}, 0, 0);
      dispatch_semaphore_signal(inFlight);
      return;
    }
    gpuMillis->store((done.GPUEndTime - done.GPUStartTime) * 1000.0);
    if (drewWorld && !failed->load()) worldFrame->store(true);
    if (onCapture) {
      const auto* bytes = static_cast<const uint8_t*>(captured.contents);
      onCapture(std::vector<uint8_t>(bytes, bytes + size_t{width} * height * 4), width, height);
    }
    dispatch_semaphore_signal(inFlight);
  }];
  [cmd commit];
}

void MetalSplatRenderer::captureNextFrame(CaptureHandler handler) {
  if (gpuFailed_.load()) {
    if (handler) handler({}, 0, 0);
    return;
  }
  capture_ = std::move(handler);
}

}  // namespace splatkit
