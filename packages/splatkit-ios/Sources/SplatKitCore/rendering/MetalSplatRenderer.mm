#include "rendering/MetalSplatRenderer.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "SplatShaderSource.h"
#include "splat/math/Mat4.h"
#include "splatkit/Log.h"

namespace splatkit {
namespace {

// The Camera buffer of the shader: two float4x4, three float2, two uints and a float4.
// Every member sits at the offset MSL gives it, so the struct is written as one block.
struct CameraUniform {
  splat::Mat4 view;
  splat::Mat4 proj;
  float focal[2];
  float tanHalfFov[2];
  float screenSize[2];
  uint32_t outputLinear;
  uint32_t pad;
  float cameraPosition[4];
};
static_assert(sizeof(CameraUniform) == 176, "CameraUniform must match the shader struct");

id<MTLBuffer> makeBuffer(id<MTLDevice> device, size_t bytes) {
  // Unified memory: a shared buffer is written by the CPU and read by the GPU with no
  // copy, which is what tiles landing in a slab and a new order every frame want.
  return [device newBufferWithLength:std::max<size_t>(bytes, 4)
                             options:MTLResourceStorageModeShared];
}

}  // namespace

std::unique_ptr<MetalSplatRenderer> MetalSplatRenderer::create() {
  std::unique_ptr<MetalSplatRenderer> r(new MetalSplatRenderer());
  r->device_ = MTLCreateSystemDefaultDevice();
  if (r->device_ == nil) {
    LOGE("no Metal device");
    return nullptr;
  }
  r->queue_ = [r->device_ newCommandQueue];
  NSError* error = nil;
  r->library_ = [r->device_ newLibraryWithSource:@(SplatShaderSource) options:nil error:&error];
  if (r->library_ == nil) {
    LOGE("shader compilation failed: %s", error.localizedDescription.UTF8String);
    return nullptr;
  }
  for (auto& uniform : r->uniforms_) uniform = makeBuffer(r->device_, sizeof(CameraUniform));
  r->inFlight_ = dispatch_semaphore_create(kFramesInFlight);
  r->description_ = std::string(r->device_.name.UTF8String) + ", Metal";
  LOGI("%s", r->description_.c_str());
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
  layer_.device = device_;
  layer_.pixelFormat = pixelFormat();
  layer_.framebufferOnly = YES;
  ++generation_;
  if (pipelineFormat_ != pixelFormat() && !createPipelines()) {
    LOGE("rendering stopped: no pipelines");
    layer_ = nil;
  }
}

void MetalSplatRenderer::setDrawableSize(uint32_t width, uint32_t height) {
  if (width == width_ && height == height_) return;
  LOGI("drawable %ux%u, was %ux%u", width, height, width_, height_);
  width_ = width;
  height_ = height;
  ++generation_;
  createTarget();
}

void MetalSplatRenderer::setRenderScale(float scale) {
  scale = std::clamp(scale, 0.1f, 2.0f);
  if (scale == renderScale_) return;
  renderScale_ = scale;
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
  // without vsync would need its own loop. Frame times are the GPU times either way.
  (void)vsync;
}

MTLPixelFormat MetalSplatRenderer::pixelFormat() const {
  return linearBlending_ ? MTLPixelFormatBGRA8Unorm_sRGB : MTLPixelFormatBGRA8Unorm;
}

Extent MetalSplatRenderer::drawExtent() const {
  if (target_ != nil) {
    return {static_cast<uint32_t>(target_.width), static_cast<uint32_t>(target_.height)};
  }
  return {width_, height_};
}

bool MetalSplatRenderer::createTarget() {
  target_ = nil;
  if (renderScale_ == 1.0f || width_ == 0 || height_ == 0) return true;
  MTLTextureDescriptor* desc = [MTLTextureDescriptor
      texture2DDescriptorWithPixelFormat:pixelFormat()
                                   width:std::max(1u, static_cast<uint32_t>(width_ * renderScale_))
                                  height:std::max(1u, static_cast<uint32_t>(height_ * renderScale_))
                               mipmapped:NO];
  desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
  desc.storageMode = MTLStorageModePrivate;
  target_ = [device_ newTextureWithDescriptor:desc];
  if (target_ == nil) {
    LOGE("render target %lux%lu failed", static_cast<unsigned long>(desc.width),
         static_cast<unsigned long>(desc.height));
  }
  return target_ != nil;
}

// One splat pipeline per spherical harmonics degree, specialised through a function
// constant so a degree 0 world pays nothing for SH, plus the render scale pass.
bool MetalSplatRenderer::createPipelines() {
  pipelineFormat_ = pixelFormat();
  NSError* error = nil;
  id<MTLFunction> fragment = [library_ newFunctionWithName:@"splatFragment"];
  for (int degree = 0; degree <= kMaxShDegree; ++degree) {
    MTLFunctionConstantValues* constants = [MTLFunctionConstantValues new];
    uint32_t value = static_cast<uint32_t>(degree);
    [constants setConstantValue:&value type:MTLDataTypeUInt atIndex:0];
    id<MTLFunction> vertex = [library_ newFunctionWithName:@"splatVertex"
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
    // "Over" compositing, back to front: out = src.a * src + (1 - src.a) * dst.
    desc.colorAttachments[0].blendingEnabled = YES;
    desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    id<MTLRenderPipelineState> state = [device_ newRenderPipelineStateWithDescriptor:desc
                                                                               error:&error];
    if (state == nil) {
      LOGE("splat pipeline degree %d: %s", degree, error.localizedDescription.UTF8String);
      return false;
    }
    splatPipelines_[static_cast<size_t>(degree)] = state;
  }
  MTLRenderPipelineDescriptor* blit = [MTLRenderPipelineDescriptor new];
  blit.vertexFunction = [library_ newFunctionWithName:@"blitVertex"];
  blit.fragmentFunction = [library_ newFunctionWithName:@"blitFragment"];
  blit.colorAttachments[0].pixelFormat = pipelineFormat_;
  blitPipeline_ = [device_ newRenderPipelineStateWithDescriptor:blit error:&error];
  if (blitPipeline_ == nil) {
    LOGE("blit pipeline: %s", error.localizedDescription.UTF8String);
    return false;
  }
  LOGI("pipelines for format %lu", static_cast<unsigned long>(pipelineFormat_));
  return true;
}

// Worlds.

bool MetalSplatRenderer::uploadWorld(const splat::SplatCloud& cloud, int maxShDegree) {
  const size_t n = cloud.count();
  const int shDegree = std::clamp(std::min(cloud.shDegree, maxShDegree), 0, kMaxShDegree);
  const std::vector<uint32_t> sh =
      carriesSh(cloud, shDegree) ? packSh(cloud, shDegree) : std::vector<uint32_t>{0};
  const std::vector<GpuSplat> packed = packSplats(cloud);

  auto world = std::make_unique<World>();
  world->count = static_cast<uint32_t>(n);
  world->shDegree = sh.size() > 1 ? shDegree : 0;
  world->splats = makeBuffer(device_, packed.size() * sizeof(GpuSplat));
  world->sh = makeBuffer(device_, sh.size() * sizeof(uint32_t));
  for (auto& order : world->orders) order = makeBuffer(device_, n * sizeof(uint32_t));
  if (world->splats == nil || world->sh == nil || world->orders[0] == nil ||
      world->orders[1] == nil) {
    LOGE("world buffers for %zu splats failed", n);
    return false;
  }
  std::memcpy(world->splats.contents, packed.data(), packed.size() * sizeof(GpuSplat));
  std::memcpy(world->sh.contents, sh.data(), sh.size() * sizeof(uint32_t));
  // Identity order until the sorter runs.
  auto* order = static_cast<uint32_t*>(world->orders[0].contents);
  for (uint32_t i = 0; i < n; ++i) order[i] = i;
  waitIdle();  // the previous world may still be in flight
  world_ = std::move(world);
  return true;
}

bool MetalSplatRenderer::createSlab(uint32_t capacity, int shDegree) {
  if (capacity == 0) return false;
  auto world = std::make_unique<World>();
  world->count = capacity;
  world->shDegree = std::clamp(shDegree, 0, kMaxShDegree);
  const size_t shBytes = world->shDegree > 0
                             ? size_t{capacity} * shStride(world->shDegree) * sizeof(uint32_t)
                             : sizeof(uint32_t);
  world->splats = makeBuffer(device_, size_t{capacity} * sizeof(GpuSplat));
  world->sh = makeBuffer(device_, shBytes);
  for (auto& order : world->orders)
    order = makeBuffer(device_, size_t{capacity} * sizeof(uint32_t));
  if (world->splats == nil || world->sh == nil || world->orders[0] == nil ||
      world->orders[1] == nil) {
    LOGE("slab of %u splats failed", capacity);
    return false;
  }
  waitIdle();
  world_ = std::move(world);
  return true;
}

bool MetalSplatRenderer::uploadTile(uint32_t offset, const splat::SplatCloud& cloud) {
  if (!world_) return false;
  const size_t n = cloud.count();
  if (n == 0) return true;
  if (offset > world_->count || n > world_->count - offset) return false;
  const std::vector<GpuSplat> packed = packSplats(cloud);
  std::memcpy(static_cast<GpuSplat*>(world_->splats.contents) + offset, packed.data(),
              packed.size() * sizeof(GpuSplat));
  if (world_->shDegree == 0) return true;
  const size_t stride = shStride(world_->shDegree);
  const std::vector<uint32_t> sh = carriesSh(cloud, world_->shDegree)
                                       ? packSh(cloud, world_->shDegree)
                                       : std::vector<uint32_t>(n * stride, 0);
  std::memcpy(static_cast<uint32_t*>(world_->sh.contents) + size_t{offset} * stride, sh.data(),
              sh.size() * sizeof(uint32_t));
  return true;
}

std::optional<GpuWorldInfo> MetalSplatRenderer::world() const {
  if (!world_) return std::nullopt;
  return GpuWorldInfo{world_->count, world_->shDegree};
}

// The frame.

bool MetalSplatRenderer::draw(const Frame& frame) {
  if (!ready() || splatPipelines_[0] == nil) return false;
  dispatch_semaphore_wait(inFlight_, DISPATCH_TIME_FOREVER);
  id<CAMetalDrawable> drawable = [layer_ nextDrawable];
  if (drawable == nil) {
    dispatch_semaphore_signal(inFlight_);
    return false;
  }
  const uint32_t slot = static_cast<uint32_t>(frame_ % kFramesInFlight);
  const Extent extent = drawExtent();

  // A new order goes into the buffer the frame in flight is not reading.
  uint32_t drawCount = 0;
  if (world_) {
    if (frame.order != nullptr) {
      const uint32_t next = world_->current ^ 1u;
      const uint32_t count = std::min(frame.orderCount, world_->count);
      std::memcpy(world_->orders[next].contents, frame.order, size_t{count} * sizeof(uint32_t));
      world_->current = next;
    }
    drawCount = std::min(frame.drawCount, world_->count);
  }

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

  id<MTLCommandBuffer> cmd = [queue_ commandBuffer];
  MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
  pass.colorAttachments[0].texture = target_ != nil ? target_ : drawable.texture;
  pass.colorAttachments[0].loadAction = MTLLoadActionClear;
  pass.colorAttachments[0].storeAction = MTLStoreActionStore;
  pass.colorAttachments[0].clearColor = MTLClearColorMake(0.05, 0.05, 0.08, 1.0);
  id<MTLRenderCommandEncoder> encoder = [cmd renderCommandEncoderWithDescriptor:pass];
  if (world_ && drawCount > 0) {
    const int degree = std::clamp(std::min(frame.shDegree, world_->shDegree), 0, kMaxShDegree);
    [encoder setRenderPipelineState:splatPipelines_[static_cast<size_t>(degree)]];
    [encoder setVertexBuffer:uniforms_[slot] offset:0 atIndex:0];
    [encoder setVertexBuffer:world_->splats offset:0 atIndex:1];
    [encoder setVertexBuffer:world_->orders[world_->current] offset:0 atIndex:2];
    [encoder setVertexBuffer:world_->sh offset:0 atIndex:3];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                vertexStart:0
                vertexCount:4
              instanceCount:drawCount];
  }
  [encoder endEncoding];

  if (target_ != nil) {
    MTLRenderPassDescriptor* blit = [MTLRenderPassDescriptor renderPassDescriptor];
    blit.colorAttachments[0].texture = drawable.texture;
    blit.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    blit.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> scale = [cmd renderCommandEncoderWithDescriptor:blit];
    [scale setRenderPipelineState:blitPipeline_];
    [scale setFragmentTexture:target_ atIndex:0];
    [scale drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [scale endEncoding];
  }

  // A capture copies the presented pixels out before the drawable goes to the screen.
  // The layer only hands out readable drawables while `framebufferOnly` is off, so it
  // is turned off for the frame after a request and back on once the copy is encoded.
  id<MTLBuffer> captured = nil;
  CaptureHandler onCapture;
  if (capture_ && !drawable.texture.framebufferOnly) {
    const NSUInteger bytesPerRow = NSUInteger{width_} * 4;
    captured = [device_ newBufferWithLength:bytesPerRow * height_
                                    options:MTLResourceStorageModeShared];
    id<MTLBlitCommandEncoder> copy = [cmd blitCommandEncoder];
    [copy copyFromTexture:drawable.texture
                     sourceSlice:0
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(0, 0, 0)
                      sourceSize:MTLSizeMake(width_, height_, 1)
                        toBuffer:captured
               destinationOffset:0
          destinationBytesPerRow:bytesPerRow
        destinationBytesPerImage:bytesPerRow * height_];
    [copy endEncoding];
    onCapture = std::move(capture_);
    capture_ = nullptr;
    layer_.framebufferOnly = YES;
  } else if (capture_) {
    layer_.framebufferOnly = NO;
  }

  [cmd presentDrawable:drawable];
  dispatch_semaphore_t inFlight = inFlight_;
  std::atomic<double>* gpuMillis = &lastGpuMillis_;
  const uint32_t width = width_;
  const uint32_t height = height_;
  [cmd addCompletedHandler:^(id<MTLCommandBuffer> done) {
    gpuMillis->store((done.GPUEndTime - done.GPUStartTime) * 1000.0);
    if (onCapture) {
      const auto* bytes = static_cast<const uint8_t*>(captured.contents);
      onCapture(std::vector<uint8_t>(bytes, bytes + size_t{width} * height * 4), width, height);
    }
    dispatch_semaphore_signal(inFlight);
  }];
  [cmd commit];
  ++frame_;
  return true;
}

void MetalSplatRenderer::captureNextFrame(CaptureHandler handler) {
  capture_ = std::move(handler);
}

}  // namespace splatkit
