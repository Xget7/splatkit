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
#include "splat/formats/SplatCloud.h"
#include "splat/math/Mat4.h"
#include "splat/math/Vec3.h"

namespace splatkit {

// Everything between an ANativeWindow and a presented frame: the surface, the swapchain,
// the offscreen target a render scale needs, the pipelines built for their format and
// the world bound to them. Survives losing and regaining the window, and the world stays
// through it. A rebuild that fails drops the surface and logs; the view stays blank
// until the host attaches a surface again. Render thread only.
class VulkanSplatRenderer {
 public:
  VulkanSplatRenderer(VulkanContext& ctx, FrameLoop& frameLoop);
  ~VulkanSplatRenderer();

  VulkanSplatRenderer(const VulkanSplatRenderer&) = delete;
  VulkanSplatRenderer& operator=(const VulkanSplatRenderer&) = delete;

  // A new window (takes a reference) or nullptr when the surface is going away.
  void setWindow(ANativeWindow* window);
  // The window changed size while staying attached. Rebuilds the swapchain if needed.
  void onSurfaceResized(uint32_t width, uint32_t height);

  // Fraction of the surface resolution the splats are drawn at, [0.1, 2]. Away from one
  // the frame is drawn offscreen and rescaled with a linear blit.
  void setRenderScale(float scale);
  float renderScale() const { return renderScale_; }
  // Blend in linear light instead of the encoded space; flips the swapchain format.
  void setLinearBlending(bool linear);
  bool linearBlending() const { return linearBlending_; }
  // Off, frame times stop being multiples of the vsync, which benchmarks need.
  void setVsync(bool vsync);

  // True when a surface with pipelines is up: frames can be drawn and worlds uploaded.
  bool ready() const { return swapchain_ && splats_ && triangle_; }
  // Where the splats are drawn: the target's size with a render scale, else the swapchain's.
  VkExtent2D drawExtent() const;
  // Counts the rebuilds of the swapchain or the target. A frame drawn before one is gone.
  uint32_t generation() const { return generation_; }

  // Uploads a world and draws it from now on, once the GPU is done with the previous one.
  // Needs `ready()`. Fails, keeping the previous world, when the upload does.
  bool uploadWorld(const splat::SplatCloud& cloud, int maxShDegree);
  const GpuWorld* world() const { return world_.get(); }

  struct Frame {
    // A new draw order for the world, copied in before the draw; nullptr keeps the last.
    const uint32_t* order = nullptr;
    uint32_t orderCount = 0;
    uint32_t drawCount = 0;  // entries of the order buffer to draw
    int shDegree = 0;        // capped by what the world carries
    splat::Mat4 view = splat::Mat4::identity();
    splat::Mat4 proj = splat::Mat4::identity();
    splat::Vec3 cameraPosition;
  };
  // Records and presents one frame: the world, or the debug triangle without one.
  // Returns false when nothing was presented, e.g. the swapchain was rebuilt instead.
  bool draw(const Frame& frame);

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
  float renderScale_ = 1.0f;
  bool linearBlending_ = false;
  bool vsync_ = true;
  uint32_t generation_ = 0;
};

}  // namespace splatkit
