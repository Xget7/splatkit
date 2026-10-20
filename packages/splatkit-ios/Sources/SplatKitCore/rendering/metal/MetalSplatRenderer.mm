#include "rendering/metal/MetalSplatRenderer.h"

#include <algorithm>
#include <cmath>
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
  r->compute_ = std::make_unique<MetalFrameCompute>(*r->context_, r->frameLoop_);
  if (!r->compute_->create()) return nullptr;
  r->surface_ = std::make_unique<MetalSurface>(r->context_->device(), r->context_->library(),
                                               r->frameLoop_, r->compute_->gpuSort());
  LOGI("%s", r->context_->description().c_str());
  return r;
}

MetalSplatRenderer::~MetalSplatRenderer() {
  // The members destroyed after this body are what in-flight frames still use.
  frameLoop_.waitIdle();
}

void MetalSplatRenderer::setLayer(CAMetalLayer* layer) {
  if (layer == surface_->layer()) return;
  bool built = surface_->setLayer(layer);
  if (layer == nil) return;
  if (pipelineFormat_ != surface_->targetFormat()) built = createPipelines() && built;
  if (!built) {
    LOGE("rendering stopped: no pipelines");
    surface_->setLayer(nil);
  }
}

void MetalSplatRenderer::setDrawableSize(uint32_t width, uint32_t height) {
  surface_->setDrawableSize(width, height);
}

void MetalSplatRenderer::setRenderScale(float scale) {
  surface_->setRenderScale(scale);
}

void MetalSplatRenderer::setSpatialUpscaling(bool enabled) {
  surface_->setSpatialUpscaling(enabled);
}

void MetalSplatRenderer::setLinearBlending(bool linear) {
  if (linear == surface_->linearBlending()) return;
  const bool blitReady = surface_->setLinearBlending(linear);
  const bool pipelinesReady = createPipelines();
  if (!blitReady || !pipelinesReady) {
    LOGE("rendering stopped: no pipelines");
    surface_->setLayer(nil);
  }
}

void MetalSplatRenderer::setVsync(bool vsync) {
  surface_->setVsync(vsync);
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
  caps.supportsSubgroups = compute_->gpuSort();
  // Metal exposes no texture-dimension query; Apple family 7, the minimum this renderer
  // accepts, supports 16384x16384.
  caps.maxTextureDimension = context_->device() != nil ? 16384u : 0u;
  RenderPolicySupport& policy = caps.policy;
  policy.fallback.raster = RasterStrategy::hardware;
  policy.fallback.tileSize = 16;
  policy.fallback.lodErrorPixels = 1.0f;
  policy.fallback.alphaThreshold = kQuadMinAlpha;
  policy.fallback.subpixelThreshold = kMinPixelRadius;
  policy.fallback.sortDepth = compute_->depthBits() == MetalRadixSort::KeyBits::Low16
                                  ? SortKeyBits::low16
                                  : SortKeyBits::full32;
  policy.fallback.enableFrustumCulling = true;
  policy.fallback.enableEarlyTermination = true;
  // The key width is a public per-instance control. The raster strategy, sub-pixel radius,
  // alpha, tile size and occlusion stay fixed shader/rasterization choices.
  // The LOD error threshold applies live to a hierarchy world and at its next upload.
  policy.lodErrorPixels = compute_->gpuSort();
  policy.minLodErrorPixels = 0.1f;
  policy.maxLodErrorPixels = 16.0f;
  policy.lodSplatLimit = compute_->gpuSort();
  policy.sortDepth = compute_->gpuSort();
  return caps;
}

bool MetalSplatRenderer::applyRenderPolicy(const RenderPolicy& policy, std::string* reason) {
  return compute_->configure(policy, reason);
}

// One splat pipeline per spherical harmonics degree, specialised through a function
// constant so a degree 0 world pays nothing for SH, and the pipelines of the GPU order path.
bool MetalSplatRenderer::createPipelines() {
  pipelineFormat_ = surface_->targetFormat();
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
  frameLoop_.waitIdle();
  if (!compute_->configureFlatWorld(world->info().count)) return false;
  world_ = std::move(world);
  frameLoop_.clearCompletedWorldFrame();
  return true;
}

bool MetalSplatRenderer::uploadLodWorld(const splat::LodTree& tree, int maxShDegree,
                                        uint32_t budget) {
  if (!compute_->gpuSort()) return false;
  auto world = MetalWorld::upload(context_->device(), tree.nodes, maxShDegree);
  if (!world) return false;
  frameLoop_.waitIdle();
  if (!compute_->configureLodWorld(tree, budget)) return false;
  world_ = std::move(world);
  frameLoop_.clearCompletedWorldFrame();
  return true;
}

bool MetalSplatRenderer::createSlab(uint32_t capacity, int shDegree) {
  auto world = MetalWorld::slab(context_->device(), capacity, shDegree);
  if (!world) return false;
  frameLoop_.waitIdle();
  if (!compute_->configureFlatWorld(capacity)) return false;
  world_ = std::move(world);
  frameLoop_.clearCompletedWorldFrame();
  return true;
}

bool MetalSplatRenderer::uploadTile(uint32_t offset, const splat::SplatCloud& cloud) {
  return world_ && world_->uploadTile(offset, cloud);
}

std::optional<GpuWorldInfo> MetalSplatRenderer::world() const {
  return world_ ? std::optional<GpuWorldInfo>{world_->info()} : std::nullopt;
}

// The frame.

uint32_t MetalSplatRenderer::takePresentTimes(std::vector<int64_t>* times) {
  return frameLoop_.takePresentTimes(times);
}

bool MetalSplatRenderer::draw(const Frame& frame) {
  if (frameLoop_.gpuFailed()) return false;
  if (!ready() || splatPipelines_[0] == nil) return false;
  if (!frameLoop_.acquire()) return false;
  // Capture requires a readable drawable before nextDrawable() acquires it.
  if (capture_) surface_->allowReadback(true);
  id<CAMetalDrawable> drawable = surface_->nextDrawable();
  if (drawable == nil) {
    // Until submitFrame(), every failure returns the acquired slot synchronously.
    frameLoop_.release();
    return false;
  }
  const uint32_t slot = compute_->slot();

  // A new order goes into the buffer the frame in flight is not reading.
  uint32_t drawCount = 0;
  if (world_) {
    if (frame.order != nullptr && !world_->writeOrder(frame.order, frame.orderCount)) {
      frameLoop_.release();
      return false;
    }
    drawCount = std::min(frame.drawCount, world_->info().count);
  }
  compute_->updateCamera(frame, slot, surface_->drawExtent(), surface_->linearBlending());

  const bool gpuOrder = world_ && frame.orderSource == OrderSource::gpu && compute_->gpuSort();
  id<MTLCommandBuffer> selection =
      gpuOrder && compute_->hasLod() ? compute_->encodeSelection(slot) : nil;
  id<MTLCommandBuffer> sort =
      gpuOrder ? compute_->encodeVisibilityAndSort(frame, slot, *world_) : nil;
  if (gpuOrder && sort == nil) {
    // Selection has only been encoded, so no GPU work can still use this slot.
    frameLoop_.release();
    return false;
  }

  id<MTLCommandBuffer> cmd = [context_->queue() commandBuffer];
  encodeRaster(cmd, drawable.texture, frame, slot, drawCount, gpuOrder);
  surface_->encodeOutput(cmd, drawable.texture);
  CaptureHandler onCapture;
  id<MTLBuffer> captured = encodeCapture(cmd, drawable.texture, &onCapture);

  // Submit dependencies in order on the same queue. The final completion releases
  // the slot only after selection, visibility/sort and rendering have finished.
  if (selection != nil) [selection commit];
  if (sort != nil) [sort commit];
  const bool drewWorld = world_ && (gpuOrder || drawCount > 0);
  submitFrame(cmd, drawable, drewWorld, captured, std::move(onCapture));
  compute_->advance();
  return true;
}

void MetalSplatRenderer::encodeRaster(id<MTLCommandBuffer> cmd, id<MTLTexture> drawableTexture,
                                      const Frame& frame, uint32_t slot, uint32_t drawCount,
                                      bool gpuOrder) {
  MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
  const MetalRenderTarget& target = surface_->target();
  id<MTLTexture> colour = target.color() != nil ? target.color() : drawableTexture;
  pass.colorAttachments[0].texture = colour;
  pass.colorAttachments[0].loadAction = MTLLoadActionClear;
  pass.colorAttachments[0].storeAction = MTLStoreActionStore;
  // Front to back accumulates onto nothing and puts the background under at the end.
  pass.colorAttachments[0].clearColor = gpuOrder ? MTLClearColorMake(0, 0, 0, 0) : kBackground;
  if (surface_->ensureDepth(colour.width, colour.height)) {
    pass.depthAttachment.texture = target.depth();
    pass.depthAttachment.loadAction = MTLLoadActionClear;
    pass.depthAttachment.storeAction = MTLStoreActionDontCare;
    pass.depthAttachment.clearDepth = 1.0;
  }
  id<MTLRenderCommandEncoder> encoder = [cmd renderCommandEncoderWithDescriptor:pass];
  if (world_ && (drawCount > 0 || gpuOrder)) {
    [encoder setVertexBuffer:compute_->uniforms(slot) offset:0 atIndex:0];
    if (gpuOrder) {
      [encoder setVertexBuffer:compute_->projected() offset:0 atIndex:1];
      [encoder setVertexBuffer:compute_->order() offset:0 atIndex:2];
      id<MTLBuffer> arguments = compute_->drawArguments(slot);
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
      const int degree = world_->drawDegree(frame.shDegree);
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

id<MTLBuffer> MetalSplatRenderer::encodeCapture(id<MTLCommandBuffer> cmd,
                                                id<MTLTexture> drawableTexture,
                                                CaptureHandler* onCapture) {
  id<MTLBuffer> captured = nil;
  if (capture_) {
    if (drawableTexture.framebufferOnly) {
      LOGW("capture skipped: the drawable is not readable yet");
    } else {
      const uint32_t width = surface_->width();
      const uint32_t height = surface_->height();
      const NSUInteger bytesPerRow = NSUInteger{width} * 4;
      captured = [context_->device() newBufferWithLength:bytesPerRow * height
                                                 options:MTLResourceStorageModeShared];
      id<MTLBlitCommandEncoder> copy = [cmd blitCommandEncoder];
      [copy copyFromTexture:drawableTexture
                       sourceSlice:0
                       sourceLevel:0
                      sourceOrigin:MTLOriginMake(0, 0, 0)
                        sourceSize:MTLSizeMake(width, height, 1)
                          toBuffer:captured
                 destinationOffset:0
            destinationBytesPerRow:bytesPerRow
          destinationBytesPerImage:bytesPerRow * height];
      [copy endEncoding];
      *onCapture = std::move(capture_);
      capture_ = nullptr;
      surface_->allowReadback(false);
    }
  }

  return captured;
}

void MetalSplatRenderer::submitFrame(id<MTLCommandBuffer> cmd, id<CAMetalDrawable> drawable,
                                     bool drewWorld, id<MTLBuffer> captured,
                                     CaptureHandler onCapture) {
  std::function<void(id<MTLCommandBuffer>)> onCompleted;
  if (onCapture) {
    const uint32_t width = surface_->width();
    const uint32_t height = surface_->height();
    onCompleted = [captured, width, height,
                   onCapture = std::move(onCapture)](id<MTLCommandBuffer> done) {
      if (done.status == MTLCommandBufferStatusError) {
        onCapture({}, 0, 0);
        return;
      }
      const auto* bytes = static_cast<const uint8_t*>(captured.contents);
      onCapture(std::vector<uint8_t>(bytes, bytes + size_t{width} * height * 4), width, height);
    };
  }
  frameLoop_.submit(cmd, drawable, drewWorld, std::move(onCompleted));
}

void MetalSplatRenderer::captureNextFrame(CaptureHandler handler) {
  if (frameLoop_.gpuFailed()) {
    if (handler) handler({}, 0, 0);
    return;
  }
  capture_ = std::move(handler);
}

}  // namespace splatkit
