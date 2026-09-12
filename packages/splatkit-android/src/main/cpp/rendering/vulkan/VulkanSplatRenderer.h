#pragma once

#include <cstdint>
#include <memory>

#include <android/native_window.h>
#include <vulkan/vulkan.h>

#include "rendering/vulkan/DebugTrianglePipeline.h"
#include "rendering/vulkan/FrameLoop.h"
#include "rendering/vulkan/RenderTarget.h"
#include "rendering/vulkan/SplatPipeline.h"
#include "rendering/vulkan/Swapchain.h"
#include "rendering/vulkan/VulkanContext.h"
#include "rendering/vulkan/VulkanFrameCompute.h"
#include "splat/formats/SplatCloud.h"
#include "splatkit/rendering/SplatRenderer.h"

namespace splatkit {

// Everything between an ANativeWindow and a presented frame: the surface, the swapchain,
// the offscreen target a render scale needs, the pipelines built for their format and
// the world bound to them. Survives losing and regaining the window, and the world stays
// through it. A rebuild that fails drops the surface and logs; the view stays blank
// until the host attaches a surface again. Render thread only.
class VulkanSplatRenderer final : public SplatRenderer {
 public:
  VulkanSplatRenderer(VulkanContext& ctx, FrameLoop& frameLoop);
  ~VulkanSplatRenderer() override;

  VulkanSplatRenderer(const VulkanSplatRenderer&) = delete;
  VulkanSplatRenderer& operator=(const VulkanSplatRenderer&) = delete;

  // A new window (takes a reference) or nullptr when the surface is going away.
  void setWindow(ANativeWindow* window);
  // The window changed size while staying attached. Rebuilds the swapchain if needed.
  void onSurfaceResized(uint32_t width, uint32_t height);

  void setRenderScale(float scale) override;
  float renderScale() const override { return renderScale_; }
  // Flips the swapchain format.
  void setLinearBlending(bool linear) override;
  bool linearBlending() const override { return linearBlending_; }
  void setVsync(bool vsync) override;

  // True when a surface with pipelines is up.
  bool ready() const override { return swapchain_ && splats_ && triangle_; }
  // The target's size with a render scale, else the swapchain's.
  Extent drawExtent() const override;
  // Counts the rebuilds of the swapchain or the target.
  uint32_t generation() const override { return generation_; }

  // Uploads once the GPU is done with the previous world. Needs `ready()`.
  bool uploadWorld(const splat::SplatCloud& cloud, int maxShDegree) override;
  bool selectsLodOnGpu() const override;
  bool uploadLodWorld(const splat::LodTree& tree, int maxShDegree, uint32_t budget) override;
  bool sortsOnGpu() const override { return compute_ != nullptr; }
  bool createSlab(uint32_t capacity, int shDegree) override;
  // A frame in flight that still names those records may draw a mix of old and new for
  // one frame.
  bool uploadTile(uint32_t offset, const splat::SplatCloud& cloud) override;
  std::optional<GpuWorldInfo> world() const override;

  // The world, or the debug triangle without one.
  bool draw(const Frame& frame) override;
  double lastGpuMillis() const override { return frameLoop_.lastGpuMillis(); }
  double lastSortMillis() const override { return compute_ ? compute_->stats().sortMillis : 0; }
  double lastSelectMillis() const override { return compute_ ? compute_->stats().selectMillis : 0; }
  uint32_t lastDrawCount() const override { return compute_ ? compute_->stats().drawn : 0; }
  uint32_t lastSelectedCount() const override { return compute_ ? compute_->stats().selected : 0; }
  const std::string& deviceDescription() const override { return ctx_.deviceDescription(); }

 private:
  bool createSurface();
  bool recreateSwapchain();
  bool createRenderTarget();
  bool createPipelines();
  void keepSurfaceIf(bool rebuilt);
  bool surfaceExtentChanged() const;
  VkFormat activeFormat() const;
  VkRenderPass activeRenderPass() const;
  void destroySurface();

  VulkanContext& ctx_;
  FrameLoop& frameLoop_;
  ANativeWindow* window_ = nullptr;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  std::unique_ptr<Swapchain> swapchain_;
  std::unique_ptr<RenderTarget> target_;  // only when renderScale_ != 1
  std::unique_ptr<DebugTrianglePipeline> triangle_;
  std::unique_ptr<SplatPipeline> splats_;
  VkFormat pipelineFormat_ = VK_FORMAT_UNDEFINED;  // the format the pipelines target
  std::unique_ptr<GpuWorld> world_;
  std::unique_ptr<VulkanFrameCompute> compute_;
  float renderScale_ = 1.0f;
  bool linearBlending_ = false;
  bool vsync_ = true;
  uint32_t generation_ = 0;
};

}  // namespace splatkit
