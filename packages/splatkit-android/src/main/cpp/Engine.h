#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <android/native_window.h>
#include <vulkan/vulkan.h>

#include "camera/WalkCamera.h"
#include "rendering/vulkan/DebugTrianglePipeline.h"
#include "rendering/vulkan/FrameLoop.h"
#include "rendering/vulkan/SplatPipeline.h"
#include "rendering/vulkan/Swapchain.h"
#include "rendering/vulkan/VulkanContext.h"
#include "splat/core/Result.h"
#include "splat/formats/SplatCloud.h"
#include "splat/math/Mat4.h"
#include "splat/sorting/AsyncSorter.h"

namespace splatkit {

// The native engine behind one SplatSurfaceView. Rendering and surface changes run on
// the render thread. `loadWorld` may run on any thread: it decodes there and leaves the
// result for the render thread to upload. Survives losing and regaining the surface.
class Engine {
 public:
  static splat::Result<std::unique_ptr<Engine>> create();
  ~Engine();

  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  // A new window (takes a reference) or nullptr when the surface is going away.
  void setWindow(ANativeWindow* window);
  void render(int64_t frameTimeNanos);
  // The window changed size while staying attached. Rebuilds the swapchain if needed.
  void onSurfaceResized(uint32_t width, uint32_t height);

  // Decodes an SPZ file. Thread safe. Errors are logged and leave the current world.
  void loadWorld(const std::uint8_t* data, std::size_t size);
  // Decodes a collider GLB and builds its grid. Thread safe; applied on the next frame.
  void loadCollider(const std::uint8_t* data, std::size_t size);

  // Input, on the render thread.
  void look(float deltaYaw, float deltaPitch) { camera_.look(deltaYaw, deltaPitch); }
  void walk(float forward, float right) { camera_.walk(forward, right); }
  void setAttitude(const float rowMajor[9]) { camera_.setAttitude(rowMajor); }
  void setMotionEnabled(bool enabled) { camera_.setMotionEnabled(enabled); }
  void setVelocity(float forward, float right) { camera_.setVelocity(forward, right); }

  // Readable from any thread. Refreshed twice a second by the render loop.
  struct Stats {
    float fps = 0;
    float frameMillis = 0;  // wall time between vsyncs, averaged over the window
    float sortMillis = 0;   // last completed sort
    uint32_t splatCount = 0;
    bool walking = false;
    bool motion = false;
  };
  Stats stats() const;
  const std::string& gpuDescription() const { return ctx_->deviceDescription(); }

 private:
  Engine() = default;
  bool createSurface();
  bool recreateSwapchain();
  bool createPipelines();
  bool surfaceExtentChanged() const;
  void destroySurface();
  void uploadPendingWorld();

  std::unique_ptr<VulkanContext> ctx_;
  std::unique_ptr<FrameLoop> frameLoop_;
  ANativeWindow* window_ = nullptr;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  std::unique_ptr<Swapchain> swapchain_;
  std::unique_ptr<DebugTrianglePipeline> triangle_;
  std::unique_ptr<SplatPipeline> splats_;
  VkFormat pipelineFormat_ = VK_FORMAT_UNDEFINED;  // swapchain format the pipelines target

  std::mutex pendingMutex_;
  std::unique_ptr<splat::SplatCloud> pendingCloud_;  // decoded, waiting for upload
  std::unique_ptr<splat::Collider> pendingCollider_;
  WalkCamera camera_;
  int64_t lastFrameNanos_ = 0;
  std::unique_ptr<splat::SplatCloud> cloud_;         // CPU copy of the current world
  std::unique_ptr<GpuWorld> world_;
  std::unique_ptr<splat::AsyncSorter> sorter_;
  std::optional<splat::Vec3> lastSortedFrom_;
  double lastSortMillis_ = 0;

  int64_t fpsWindowStart_ = 0;
  uint32_t fpsWindowFrames_ = 0;
  uint32_t fpsWindowsSinceLog_ = 0;
  // Written by the render thread, read by the UI thread through stats().
  std::atomic<float> statFps_{0};
  std::atomic<float> statFrameMillis_{0};
  std::atomic<float> statSortMillis_{0};
  std::atomic<uint32_t> statSplats_{0};
  std::atomic<bool> statWalking_{false};
  std::atomic<bool> statMotion_{false};
};

}  // namespace splatkit
