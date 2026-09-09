#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <android/native_window.h>
#include <vulkan/vulkan.h>

#include "camera/WalkCamera.h"
#include "diagnostics/Benchmark.h"
#include "diagnostics/StatsPublisher.h"
#include "rendering/vulkan/FrameLoop.h"
#include "rendering/vulkan/SurfaceRenderer.h"
#include "rendering/vulkan/VulkanContext.h"
#include "splat/core/Result.h"
#include "splat/loading/WorldLoader.h"
#include "splat/math/Mat4.h"
#include "splat/sorting/AsyncSorter.h"
#include "splat/sorting/VisibilityPlanner.h"

namespace splatkit {

// The native engine behind one SplatSurfaceView. It owns the loader, the camera, the
// sorter and the renderer and runs them once per vsync: a frame steps the camera, asks
// the sorter for the visible set when the view changed enough, and draws only when
// something visible changed, so a still scene costs no GPU time.
//
// Rendering, input and settings run on the render thread. Loading may run on any
// thread: it decodes there and leaves the result for the render thread to upload.
// Survives losing and regaining the surface.
class Engine {
 public:
  using Stats = splatkit::Stats;
  using CameraPose = splatkit::CameraPose;

  static splat::Result<std::unique_ptr<Engine>> create();
  ~Engine();

  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  // A new window (takes a reference) or nullptr when the surface is going away.
  void setWindow(ANativeWindow* window);
  void render(int64_t frameTimeNanos);
  // The window changed size while staying attached. Rebuilds the swapchain if needed.
  void onSurfaceResized(uint32_t width, uint32_t height);

  // Decodes an SPZ world. Thread safe. Errors are reported and leave the current world.
  void loadWorld(const std::uint8_t* data, std::size_t size);
  // Decodes a collider GLB and builds its grid. Thread safe; applied on the next frame.
  void loadCollider(const std::uint8_t* data, std::size_t size);
  // The same from a file, mapped rather than copied through the host's heap.
  void loadWorldFile(const std::string& path);
  void loadColliderFile(const std::string& path);

  // Set on the render thread; read from any thread, refreshed every frame.
  void setCameraPose(const CameraPose& pose);
  CameraPose cameraPose() const { return stats_.pose(); }

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
  void setRenderScale(float scale) { renderer_->setRenderScale(scale); }
  float renderScale() const { return renderer_->renderScale(); }

  // Base angular margin around the view, in degrees, that the cull keeps drawn so that
  // what turns into view before the next cull lands is already there; a fast turn adds
  // to it. Wider costs draws that are off screen, narrower risks an empty edge on a
  // flick. Render thread.
  void setCullMargin(float degrees) { planner_.setBaseMargin(degrees); }
  float cullMargin() const { return planner_.baseMargin(); }

  // Blend splats in linear light instead of the encoded space the training used. Richer
  // contrast at the cost of 40% of the frame on Adreno 640, and not what the reference
  // rasterizer produces; off by default (ADR 0011). Render thread.
  void setLinearBlending(bool linear) { renderer_->setLinearBlending(linear); }
  bool linearBlending() const { return renderer_->linearBlending(); }

  // Level of detail budget: the most splats drawn per frame, or 0 to draw every splat.
  // A world loaded with a budget gets a hierarchy built over it (about 1.5 times the
  // splats in GPU memory), and each frame draws the nodes that cover the scene at about
  // a pixel each, nearest in full detail. Applies to worlds loaded after it is set.
  void setSplatBudget(int budget) { loader_.setBudget(budget); }

  // Highest spherical harmonics degree uploaded with the next world, 0 to 3. Degree 3
  // adds 92 bytes per splat; 0 keeps the base colour only. Any thread.
  void setMaxShDegree(int degree) { maxShDegree_ = std::clamp(degree, 0, kMaxShDegree); }

  // Spherical harmonics degree drawn, 0 to 3, capped by what the loaded world carries.
  // Takes effect on the next frame: a quality change never needs a reload. Render thread.
  void setShDegree(int degree);

  // Input, on the render thread.
  void look(float deltaYaw, float deltaPitch) { camera_.look(deltaYaw, deltaPitch); }
  void walk(float forward, float right) { camera_.walk(forward, right); }
  void setAttitude(const float rowMajor[9]) { camera_.setAttitude(rowMajor); }
  void setMotionEnabled(bool enabled) { camera_.setMotionEnabled(enabled); }
  void setVelocity(float forward, float right) { camera_.setVelocity(forward, right); }

  // Readable from any thread. Refreshed twice a second by the render loop.
  Stats stats() const { return stats_.stats(); }

  // Runs a reproducible capture: gyroscope off, a fixed pose, one full yaw turn over
  // `seconds`, then logs the frame time distribution. Waits for a world if none is up.
  void startBenchmark(float seconds);
  const std::string& gpuDescription() const { return ctx_->deviceDescription(); }

 private:
  static constexpr int kMaxShDegree = 3;

  // The camera as the frame sees it: matrices for the draw, axes for the cull.
  struct FrameCamera {
    splat::Mat4 view;
    splat::Mat4 proj;
    splat::VisibilityPlanner::View axes;
  };

  Engine() = default;
  void emit(Event event, const std::string& message = {}, uint32_t splatCount = 0) const {
    if (events_) events_(event, message, splatCount);
  }
  void reportWorld(const splat::Result<splat::WorldLoader::WorldReport>& report);
  void reportCollider(const splat::Result<splat::WorldLoader::ColliderReport>& report);
  bool applyPendingLoads();
  float frameSeconds(int64_t frameTimeNanos);
  void driveBenchmark(float dt, const GpuWorld& world);
  FrameCamera frameCamera(VkExtent2D extent) const;
  void publishPose();
  void requestVisible(const FrameCamera& camera, float dt, VkExtent2D extent);
  void takeSortResult();
  StatsPublisher::Sample sample() const;

  EventSink events_;
  std::unique_ptr<VulkanContext> ctx_;
  std::unique_ptr<FrameLoop> frameLoop_;
  std::unique_ptr<SurfaceRenderer> renderer_;  // after ctx_ and frameLoop_: dies first
  splat::WorldLoader loader_;
  WalkCamera camera_;
  splat::VisibilityPlanner planner_;
  Benchmark benchmark_;
  StatsPublisher stats_;

  std::atomic<int> maxShDegree_{kMaxShDegree};
  int shDegree_ = kMaxShDegree;
  // Render thread from here on.
  std::unique_ptr<splat::AsyncSorter> sorter_;
  int loadedBudget_ = 0;      // the budget of the world on the GPU, 0 without a tree
  uint32_t sourceCount_ = 0;  // splats in the loaded file, what hosts and the HUD count
  uint32_t drawCount_ = 0;    // entries of the order buffer to draw: the visible splats
  std::optional<splat::AsyncSorter::Result> pendingOrder_;  // sorted, waiting for a frame
  struct SortTimings {
    double sortMillis = 0;
    double cullMillis = 0;
    double selectMillis = 0;
    std::size_t selected = 0;
  } lastSort_;
  int64_t lastFrameNanos_ = 0;
  bool redrawNeeded_ = true;
  splat::Mat4 lastDrawnView_ = splat::Mat4::identity();
  uint32_t lastDrawnGeneration_ = 0;
};

}  // namespace splatkit
