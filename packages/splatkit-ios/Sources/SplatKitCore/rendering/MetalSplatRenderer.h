#pragma once

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "rendering/MetalVisibility.h"
#include "splatkit/rendering/GpuLayout.h"
#include "splatkit/rendering/SplatRenderer.h"

namespace splatkit {

// Everything between a CAMetalLayer and a presented frame: the device and queue, the
// pipelines built for the layer's pixel format, the offscreen target a render scale
// needs, and the world bound to them. The layer is attached and sized by the view; the
// world stays through a detach. Render thread only, except where noted.
class MetalSplatRenderer final : public SplatRenderer {
 public:
  // Null when the device has no Metal.
  static std::unique_ptr<MetalSplatRenderer> create();
  ~MetalSplatRenderer() override;

  MetalSplatRenderer(const MetalSplatRenderer&) = delete;
  MetalSplatRenderer& operator=(const MetalSplatRenderer&) = delete;

  // The layer to present to, or nil when the view is going away. The renderer sets its
  // device and pixel format; the view sets its size through `setDrawableSize`.
  void setLayer(CAMetalLayer* layer);
  // The layer's size in pixels. Rebuilds the offscreen target if there is one.
  void setDrawableSize(uint32_t width, uint32_t height);

  void setRenderScale(float scale) override;
  float renderScale() const override { return renderScale_; }
  void setLinearBlending(bool linear) override;
  bool linearBlending() const override { return linearBlending_; }
  void setVsync(bool vsync) override;

  bool ready() const override { return layer_ != nil && width_ > 0 && height_ > 0; }
  Extent drawExtent() const override;
  uint32_t generation() const override { return generation_; }

  bool uploadWorld(const splat::SplatCloud& cloud, int maxShDegree) override;
  bool createSlab(uint32_t capacity, int shDegree) override;
  bool uploadTile(uint32_t offset, const splat::SplatCloud& cloud) override;
  std::optional<GpuWorldInfo> world() const override;

  bool draw(const Frame& frame) override;

  // Pixels of a presented frame: BGRA, 8 bits each, rows top down, `width` by `height`.
  using CaptureHandler =
      std::function<void(std::vector<uint8_t> bgra, uint32_t width, uint32_t height)>;
  // Hands the next frame's pixels to `handler`, from the GPU's completion thread. One
  // capture at a time; a request while one is pending replaces it.
  void captureNextFrame(CaptureHandler handler);
  double lastGpuMillis() const override { return lastGpuMillis_.load(); }
  // The cull and the sort run as compute passes on the GPU (MetalVisibility); the engine
  // hands over the ranges to draw and never sorts on the CPU for this renderer.
  bool sortsOnGpu() const override { return gpuSort_; }
  double lastSortMillis() const override { return lastSortMillis_.load(); }
  uint32_t lastDrawCount() const override { return lastDrawCount_.load(); }
  const std::string& deviceDescription() const override { return description_; }

  static constexpr int kMaxShDegree = 3;
  static constexpr uint32_t kFramesInFlight = MetalVisibility::kSlots;

 private:
  MetalSplatRenderer() = default;
  bool createPipelines();
  bool createTarget();
  MTLPixelFormat pixelFormat() const;
  void waitIdle();

  // The world on the GPU: the records, the harmonics, and two order buffers so that a
  // new order is written while the frame in flight still reads the previous one.
  struct World {
    id<MTLBuffer> splats = nil;
    id<MTLBuffer> sh = nil;
    std::array<id<MTLBuffer>, 2> orders{};
    uint32_t current = 0;  // the order buffer the last frame drew from
    uint32_t count = 0;
    int shDegree = 0;
  };

  id<MTLDevice> device_ = nil;
  id<MTLCommandQueue> queue_ = nil;
  id<MTLLibrary> library_ = nil;
  std::array<id<MTLRenderPipelineState>, kMaxShDegree + 1> splatPipelines_{};
  id<MTLRenderPipelineState> blitPipeline_ = nil;
  MTLPixelFormat pipelineFormat_ = MTLPixelFormatInvalid;
  std::array<id<MTLBuffer>, kFramesInFlight> uniforms_{};
  dispatch_semaphore_t inFlight_ = nullptr;

  CAMetalLayer* layer_ = nil;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  id<MTLTexture> target_ = nil;  // only when renderScale_ != 1
  std::unique_ptr<World> world_;
  float renderScale_ = 1.0f;
  bool linearBlending_ = false;
  uint32_t generation_ = 0;
  uint64_t frame_ = 0;
  std::atomic<double> lastGpuMillis_{0};
  MetalVisibility visibility_;
  bool gpuSort_ = false;
  std::atomic<double> lastSortMillis_{0};
  std::atomic<uint32_t> lastDrawCount_{0};
  CaptureHandler capture_;
  std::string description_;
};

}  // namespace splatkit
