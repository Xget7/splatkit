#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
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
#include "rendering/vulkan/RenderTarget.h"
#include "rendering/vulkan/SplatPipeline.h"
#include "rendering/vulkan/Swapchain.h"
#include "rendering/vulkan/VulkanContext.h"
#include "splat/core/Result.h"
#include "splat/formats/SplatCloud.h"
#include "splat/math/Mat4.h"
#include "splat/lod/LodTree.h"
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

  // Decodes an SPZ file. Thread safe. Errors are reported and leave the current world.
  void loadWorld(const std::uint8_t* data, std::size_t size);
  // Decodes a collider GLB and builds its grid. Thread safe; applied on the next frame.
  void loadCollider(const std::uint8_t* data, std::size_t size);
  // The same from a file, mapped rather than copied through the host's heap.
  void loadWorldFile(const std::string& path);
  void loadColliderFile(const std::string& path);

  // Camera pose: position in the world's frame (meters) and yaw and pitch in radians,
  // yaw about the up axis, pitch clamped to 85 degrees. Set on the render thread; read
  // from any thread, refreshed every frame.
  struct CameraPose {
    float x = 0, y = 0, z = 0, yaw = 0, pitch = 0;
  };
  void setCameraPose(const CameraPose& pose);
  CameraPose cameraPose() const;

  // What the host needs to know about loading. Ready events fire on the render thread
  // once the data is in use; failures fire on whichever thread found them.
  enum class Event { worldReady = 0, worldFailed = 1, colliderReady = 2, colliderFailed = 3 };
  using EventSink = std::function<void(Event, const std::string& message, uint32_t splatCount)>;
  void setEventSink(EventSink sink) { events_ = std::move(sink); }

  // Fraction of the surface resolution the splats are drawn at, [0.1, 2]. Away from one
  // the frame is drawn offscreen and rescaled with a linear blit: below one it is cheaper
  // (blended fragments bound splat rendering, so this is the direct lever on frame
  // time), above one it supersamples, which steadies thin splats that shimmer at a
  // pixel each. Render thread.
  void setRenderScale(float scale);
  float renderScale() const { return renderScale_; }

  // Base angular margin around the view, in degrees, that the cull keeps drawn so that
  // what turns into view before the next cull lands is already there; a fast turn adds
  // to it. Wider costs draws that are off screen, narrower risks an empty edge on a
  // flick. Render thread.
  void setCullMargin(float degrees);
  float cullMargin() const { return cullMarginDegrees_; }

  // Blend splats in linear light instead of the encoded space the training used. Richer
  // contrast at the cost of 40% of the frame on Adreno 640, and not what the reference
  // rasterizer produces; off by default (ADR 0011). Render thread.
  void setLinearBlending(bool linear);
  bool linearBlending() const { return linearBlending_; }

  // Level of detail budget: the most splats drawn per frame, or 0 to draw every splat.
  // A world loaded with a budget gets a hierarchy built over it (about 1.5 times the
  // splats in GPU memory), and each frame draws the nodes that cover the scene at about
  // a pixel each, nearest in full detail. Applies to worlds loaded after it is set.
  void setSplatBudget(int budget) { splatBudget_ = std::max(budget, 0); }

  // Highest spherical harmonics degree uploaded with the next world, 0 to 3. Degree 3
  // adds 92 bytes per splat; 0 keeps the base colour only. Any thread.
  void setMaxShDegree(int degree) { maxShDegree_ = std::clamp(degree, 0, 3); }

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
    float gpuMillis = 0;    // GPU time of the last frame, from timestamp queries
    float sortMillis = 0;   // last completed sort
    uint32_t splatCount = 0;
    bool walking = false;
    bool motion = false;
  };
  Stats stats() const;

  // Runs a reproducible capture: gyroscope off, a fixed pose, one full yaw turn over
  // `seconds`, then logs the frame time distribution. Waits for a world if none is up.
  void startBenchmark(float seconds);
  const std::string& gpuDescription() const { return ctx_->deviceDescription(); }

 private:
  Engine() = default;
  void emit(Event event, const std::string& message = {}, uint32_t splatCount = 0) const {
    if (events_) events_(event, message, splatCount);
  }
  EventSink events_;
  bool createSurface();
  bool recreateSwapchain();
  bool createPipelines();
  bool surfaceExtentChanged() const;
  bool createRenderTarget();
  VkFormat activeFormat() const;
  void destroySurface();
  bool uploadPendingWorld();
  void publishStats(int64_t frameTimeNanos, bool rendered);
  void updateBenchmark(float dt);

  std::unique_ptr<VulkanContext> ctx_;
  std::unique_ptr<FrameLoop> frameLoop_;
  ANativeWindow* window_ = nullptr;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  std::unique_ptr<Swapchain> swapchain_;
  std::unique_ptr<RenderTarget> target_;  // only when renderScale_ != 1
  float renderScale_ = 1.0f;
  float cullMarginDegrees_ = 10.0f;
  bool linearBlending_ = false;
  std::atomic<int> maxShDegree_{3};
  std::atomic<int> splatBudget_{0};
  std::unique_ptr<DebugTrianglePipeline> triangle_;
  std::unique_ptr<SplatPipeline> splats_;
  VkFormat pipelineFormat_ = VK_FORMAT_UNDEFINED;  // swapchain format the pipelines target

  std::mutex pendingMutex_;
  std::unique_ptr<splat::SplatCloud> pendingCloud_;  // decoded, waiting for upload
  std::shared_ptr<const splat::LodTree> pendingTree_;  // instead of the cloud, with a budget
  int loadedBudget_ = 0;  // the budget the current world was loaded with, 0 without a tree
  uint32_t sourceCount_ = 0;  // splats in the loaded file, what hosts and the HUD count
  std::unique_ptr<splat::Collider> pendingCollider_;
  WalkCamera camera_;
  int64_t lastFrameNanos_ = 0;
  std::unique_ptr<GpuWorld> world_;
  std::unique_ptr<splat::AsyncSorter> sorter_;
  std::optional<splat::Vec3> lastSortedFrom_;
  splat::Vec3 lastSortedForward_;
  splat::Vec3 lastFrameForward_{0, 0, -1};
  float turnRate_ = 0.0f;  // radians per second, decays after a turn
  uint32_t drawCount_ = 0;  // entries of the order buffer to draw: the visible splats
  std::optional<splat::AsyncSorter::Result> pendingOrder_;  // sorted, waiting for a command buffer
  bool redrawNeeded_ = true;
  bool lastLoggedIdle_ = false;
  splat::Mat4 lastDrawnView_ = splat::Mat4::identity();
  VkExtent2D lastDrawnExtent_{};
  double lastSortMillis_ = 0;
  double lastCullMillis_ = 0;
  double lastSelectMillis_ = 0;
  std::size_t lastSelected_ = 0;

  bool vsync_ = true;  // benchmarks turn it off so frame times are not vsync multiples
  bool benchmarkPending_ = false;
  bool benchmarkRunning_ = false;
  float benchmarkSeconds_ = 0;
  float benchmarkElapsed_ = 0;
  std::vector<float> benchmarkFrameMillis_;
  std::vector<float> benchmarkGpuMillis_;

  int64_t fpsWindowStart_ = 0;
  uint32_t fpsWindowFrames_ = 0;
  uint32_t fpsWindowsSinceLog_ = 0;
  // Written by the render thread, read by the UI thread through stats().
  std::atomic<float> statFps_{0};
  std::atomic<float> statFrameMillis_{0};
  std::atomic<float> statGpuMillis_{0};
  std::atomic<float> statSortMillis_{0};
  std::atomic<uint32_t> statSplats_{0};
  std::atomic<bool> statWalking_{false};
  std::atomic<bool> statMotion_{false};
  std::atomic<float> statPose_[5]{};
};

}  // namespace splatkit
