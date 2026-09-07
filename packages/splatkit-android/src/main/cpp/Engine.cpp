#include "Engine.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <fstream>
#include <string>
#include <algorithm>
#include <cmath>
#include <optional>

#include <vulkan/vulkan_android.h>

#include "Log.h"
#include "splat/formats/GlbDecoder.h"
#include "splat/formats/SplatDecoder.h"
#include "splat/math/Frustum.h"
#include "splat/sorting/SpatialOrder.h"
#include "splat/math/Mat4.h"

namespace splatkit {
namespace {

// The sorter's frustum is this much wider than the view (on the tangent of the half
// angles), and a new sort is requested once the view turned about 5 degrees.
// The cull keeps a margin around the view so that what turns into view before the next
// cull lands is already drawn. The base (Engine::setCullMargin, 10 degrees by default)
// covers splats whose centre is just outside the view but whose extent is not, plus a
// slow turn; the rest scales with how fast the camera is turning, over the time a cull
// result takes to reach the screen.
constexpr float kCullStaleSeconds = 0.05f;
constexpr float kMaxCullMarginDegrees = 80.0f;
constexpr float kDegreesToRadians = static_cast<float>(M_PI) / 180.0f;
// A cull is cheap, so a one degree turn asks for a new one.
constexpr float kRecullCosine = 0.99985f;

using Clock = std::chrono::steady_clock;

double millisSince(Clock::time_point t) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

// GPU temperature in degrees Celsius from the thermal zones, or a negative value when
// unavailable. Benchmarks log it because the GPU throttles when hot and every number
// taken above roughly 60 degrees on Adreno is a number about the throttling.
float gpuTemperatureCelsius() {
  for (int i = 0; i < 120; ++i) {
    const std::string base = "/sys/class/thermal/thermal_zone" + std::to_string(i);
    std::ifstream type(base + "/type");
    std::string name;
    if (!type || !std::getline(type, name)) break;
    if (name.rfind("gpuss-0", 0) != 0 && name != "gpu") continue;
    std::ifstream temp(base + "/temp");
    long milli = 0;
    if (temp >> milli) return static_cast<float>(milli) / 1000.0f;
  }
  return -1.0f;
}

bool isSrgb(VkFormat format) {
  return format == VK_FORMAT_R8G8B8A8_SRGB || format == VK_FORMAT_B8G8R8A8_SRGB;
}

}  // namespace

splat::Result<std::unique_ptr<Engine>> Engine::create() {
  std::unique_ptr<Engine> engine(new Engine());
  auto ctx = VulkanContext::create();
  if (!ctx) return ctx.error();
  engine->ctx_ = std::move(ctx.value());
  engine->frameLoop_ = std::make_unique<FrameLoop>(*engine->ctx_);
  if (!engine->frameLoop_->valid()) {
    return splat::Error{splat::ErrorCode::gpuUnavailable, "frame loop"};
  }
  return engine;
}

Engine::~Engine() {
  setWindow(nullptr);
  if (ctx_) ctx_->waitIdle();
  world_.reset();
}

void Engine::setWindow(ANativeWindow* window) {
  if (window == window_) return;
  destroySurface();
  if (window_ != nullptr) {
    ANativeWindow_release(window_);
    window_ = nullptr;
  }
  if (window == nullptr) return;

  ANativeWindow_acquire(window);
  window_ = window;
  if (!createSurface() || !recreateSwapchain()) {
    LOGE("could not attach to the surface");
    destroySurface();
  }
}

bool Engine::createSurface() {
  VkAndroidSurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
  info.window = window_;
  if (vkCreateAndroidSurfaceKHR(ctx_->instance(), &info, nullptr, &surface_) != VK_SUCCESS) {
    LOGE("vkCreateAndroidSurfaceKHR failed");
    return false;
  }
  if (!ctx_->supportsPresent(surface_)) {
    LOGE("graphics queue cannot present to this surface");
    return false;
  }
  return true;
}

bool Engine::recreateSwapchain() {
  redrawNeeded_ = true;
  ctx_->waitIdle();
  VkSwapchainKHR previous = swapchain_ ? swapchain_->release() : VK_NULL_HANDLE;
  swapchain_.reset();

  auto sc = Swapchain::create(*ctx_, surface_, previous, vsync_, linearBlending_);
  if (previous != VK_NULL_HANDLE) vkDestroySwapchainKHR(ctx_->device(), previous, nullptr);
  if (!sc) {
    LOGE("%s", sc.error().message.c_str());
    return false;
  }
  swapchain_ = std::move(sc.value());
  if (!frameLoop_->onSwapchainCreated(*swapchain_)) return false;
  if (!createRenderTarget()) return false;

  // Viewport and scissor are dynamic, so a resize or rotation does not touch the
  // pipelines. A render pass with the same attachment format is compatible with the one
  // they were built against (Vulkan 1.1, 8.2 "Render Pass Compatibility"). Only a format
  // change, which also flips the sRGB output path, forces a rebuild.
  if (splats_ && triangle_ && pipelineFormat_ == activeFormat()) return true;
  return createPipelines();
}

VkFormat Engine::activeFormat() const {
  return target_ ? target_->format() : swapchain_->format();
}

bool Engine::createRenderTarget() {
  target_.reset();
  if (renderScale_ == 1.0f) return true;
  const VkExtent2D full = swapchain_->extent();
  VkExtent2D scaled{std::max(1u, static_cast<uint32_t>(full.width * renderScale_)),
                    std::max(1u, static_cast<uint32_t>(full.height * renderScale_))};
  auto target = RenderTarget::create(*ctx_, swapchain_->format(), scaled);
  if (!target) {
    LOGE("%s", target.error().message.c_str());
    return false;
  }
  target_ = std::move(target.value());
  return true;
}

void Engine::setLinearBlending(bool linear) {
  if (linear == linearBlending_) return;
  linearBlending_ = linear;
  if (swapchain_) recreateSwapchain();
}

void Engine::setCullMargin(float degrees) {
  cullMarginDegrees_ = std::clamp(degrees, 0.0f, kMaxCullMarginDegrees);
  lastSortedForward_ = {0.0f, 0.0f, 0.0f};  // so the next frame culls with the new margin
}

void Engine::setRenderScale(float scale) {
  scale = std::clamp(scale, 0.1f, 2.0f);
  if (scale == renderScale_) return;
  renderScale_ = scale;
  if (!swapchain_) return;
  ctx_->waitIdle();
  createRenderTarget();
  if (pipelineFormat_ != activeFormat()) createPipelines();
}

bool Engine::createPipelines() {
  triangle_.reset();
  splats_.reset();
  pipelineFormat_ = activeFormat();
  const VkRenderPass pass = target_ ? target_->renderPass() : swapchain_->renderPass();
  LOGI("pipelines for format %d, offscreen %d", static_cast<int>(pipelineFormat_), target_ ? 1 : 0);

  auto triangle = DebugTrianglePipeline::create(*ctx_, pass);
  if (!triangle) {
    LOGE("%s", triangle.error().message.c_str());
    return false;
  }
  triangle_ = std::move(triangle.value());

  auto splats = SplatPipeline::create(*ctx_, pass, isSrgb(pipelineFormat_));
  if (!splats) {
    LOGE("%s", splats.error().message.c_str());
    return false;
  }
  splats_ = std::move(splats.value());
  if (world_) splats_->bindWorld(*world_);
  return true;
}

void Engine::onSurfaceResized(uint32_t width, uint32_t height) {
  if (!swapchain_) return;
  const VkExtent2D current = swapchain_->extent();
  if (current.width == width && current.height == height) return;
  LOGI("surface resized to %ux%u, swapchain was %ux%u", width, height, current.width, current.height);
  recreateSwapchain();
}

// True when the surface no longer has the swapchain's size, which is how a rotation shows
// up when the driver only answers SUBOPTIMAL. A bare SUBOPTIMAL with the same size is the
// identity pre-transform being second best, and is not worth a rebuild.
bool Engine::surfaceExtentChanged() const {
  VkSurfaceCapabilitiesKHR caps{};
  if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx_->physicalDevice(), surface_, &caps) != VK_SUCCESS) {
    return false;
  }
  const VkExtent2D current = swapchain_->extent();
  const bool changed =
      caps.currentExtent.width != current.width || caps.currentExtent.height != current.height;
  if (changed) {
    LOGI("surface is now %ux%u, swapchain was %ux%u", caps.currentExtent.width,
         caps.currentExtent.height, current.width, current.height);
  }
  return changed;
}

void Engine::destroySurface() {
  if (ctx_) ctx_->waitIdle();
  triangle_.reset();
  splats_.reset();
  target_.reset();
  swapchain_.reset();
  if (surface_ != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(ctx_->instance(), surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
  }
}

void Engine::loadWorld(const std::uint8_t* data, std::size_t size) {
  const auto start = Clock::now();
  auto decoded = splat::decodeSplatFile(data, size);
  if (!decoded) {
    LOGE("world decode failed: %s", decoded.error().message.c_str());
    emit(Event::worldFailed, decoded.error().message);
    return;
  }
  auto cloud = std::make_unique<splat::SplatCloud>(std::move(decoded.value()));
  LOGI("decoded %zu splats in %.0f ms, sh degree %d, bounds y [%.2f, %.2f]", cloud->count(),
       millisSince(start), cloud->shDegree, cloud->bounds.min[1], cloud->bounds.max[1]);
  const auto reorderStart = Clock::now();
  splat::reorderSpatially(*cloud);
  LOGI("reordered spatially in %.0f ms", millisSince(reorderStart));
  const int budget = splatBudget_.load();
  std::shared_ptr<const splat::LodTree> tree;
  if (budget > 0) {
    const auto treeStart = Clock::now();
    tree = std::make_shared<const splat::LodTree>(splat::buildLodTree(std::move(*cloud)));
    cloud.reset();
    LOGI("level of detail tree: %zu nodes over %zu splats, built in %.0f ms", tree->nodeCount(), tree->leafCount,
         millisSince(treeStart));
  }
  std::lock_guard<std::mutex> lock(pendingMutex_);
  pendingCloud_ = std::move(cloud);
  pendingTree_ = std::move(tree);
  loadedBudget_ = budget;
}

namespace {

// Maps a whole file read only, hands it to `use`, unmaps. The mapping is the decoder's
// input and nothing keeps it, so the pages leave with the call.
template <typename Use>
void withMappedFile(const std::string& path, const char* what, Use use,
                    const std::function<void(const std::string&)>& fail) {
  const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    fail(std::string("cannot open ") + what + " file: " + path);
    return;
  }
  struct stat st{};
  if (fstat(fd, &st) != 0 || st.st_size <= 0) {
    close(fd);
    fail(std::string("empty ") + what + " file: " + path);
    return;
  }
  const auto size = static_cast<std::size_t>(st.st_size);
  void* mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if (mapped == MAP_FAILED) {
    fail(std::string("cannot map ") + what + " file: " + path);
    return;
  }
  madvise(mapped, size, MADV_SEQUENTIAL);
  use(static_cast<const std::uint8_t*>(mapped), size);
  munmap(mapped, size);
}

}  // namespace

void Engine::loadWorldFile(const std::string& path) {
  withMappedFile(
      path, "world", [this](const std::uint8_t* data, std::size_t size) { loadWorld(data, size); },
      [this](const std::string& message) {
        LOGE("%s", message.c_str());
        emit(Event::worldFailed, message);
      });
}

void Engine::loadColliderFile(const std::string& path) {
  withMappedFile(
      path, "collider", [this](const std::uint8_t* data, std::size_t size) { loadCollider(data, size); },
      [this](const std::string& message) {
        LOGE("%s", message.c_str());
        emit(Event::colliderFailed, message);
      });
}

void Engine::setCameraPose(const CameraPose& pose) {
  camera_.setPosition({pose.x, pose.y, pose.z});
  camera_.setOrientation(pose.yaw, pose.pitch);
  lastSortedFrom_.reset();  // a teleport needs a fresh sort, not a cull
  redrawNeeded_ = true;
}

Engine::CameraPose Engine::cameraPose() const {
  CameraPose pose;
  pose.x = statPose_[0].load(std::memory_order_relaxed);
  pose.y = statPose_[1].load(std::memory_order_relaxed);
  pose.z = statPose_[2].load(std::memory_order_relaxed);
  pose.yaw = statPose_[3].load(std::memory_order_relaxed);
  pose.pitch = statPose_[4].load(std::memory_order_relaxed);
  return pose;
}

void Engine::loadCollider(const std::uint8_t* data, std::size_t size) {
  const auto start = Clock::now();
  auto decoded = splat::decodeGlb(data, size);
  if (!decoded) {
    LOGE("collider decode failed: %s", decoded.error().message.c_str());
    emit(Event::colliderFailed, decoded.error().message);
    return;
  }
  auto collider = std::make_unique<splat::Collider>(decoded.value());
  LOGI("collider: %zu triangles, grid built in %.0f ms", collider->triangleCount(), millisSince(start));
  std::lock_guard<std::mutex> lock(pendingMutex_);
  pendingCollider_ = std::move(collider);
}

bool Engine::uploadPendingWorld() {
  std::unique_ptr<splat::SplatCloud> cloud;
  std::shared_ptr<const splat::LodTree> tree;
  std::unique_ptr<splat::Collider> collider;
  {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    cloud = std::move(pendingCloud_);
    tree = std::move(pendingTree_);
    collider = std::move(pendingCollider_);
  }
  if (collider) {
    camera_.setCollider(std::move(collider));
    emit(Event::colliderReady);
  }
  if ((!cloud && !tree) || !splats_) return false;

  const auto start = Clock::now();
  auto world = splats_->uploadWorld(tree ? tree->nodes : *cloud, maxShDegree_.load());
  if (!world) {
    LOGE("world upload failed");
    emit(Event::worldFailed, "GPU upload failed");
    return false;
  }
  ctx_->waitIdle();  // the previous world may still be in flight
  world_ = std::move(world);
  splats_->bindWorld(*world_);
  // The sorter keeps its own copy of the positions; nothing else needs the cloud now.
  // With a tree the sorter keeps the tree, whose attributes are already on the GPU.
  sorter_ = tree ? std::make_unique<splat::AsyncSorter>(tree) : std::make_unique<splat::AsyncSorter>(cloud->positions);
  // Hosts count the file's splats; with a tree the GPU holds about 1.5 times as many nodes.
  sourceCount_ = tree ? static_cast<uint32_t>(tree->leafCount) : world_->count;
  cloud.reset();
  tree.reset();
  lastSortedFrom_.reset();
  drawCount_ = 0;  // the first frustum sort decides what is visible
  LOGI("uploaded %u splats in %.0f ms, sh degree %d", world_->count, millisSince(start), world_->shDegree);
  emit(Event::worldReady, {}, sourceCount_);
  return true;
}

void Engine::startBenchmark(float seconds) {
  benchmarkSeconds_ = seconds;
  benchmarkPending_ = true;
  benchmarkRunning_ = false;
  benchmarkFrameMillis_.clear();
  benchmarkGpuMillis_.clear();
  if (vsync_) {
    vsync_ = false;
    if (swapchain_) recreateSwapchain();
  }
  LOGI("benchmark queued: %.0f s, waiting for a world", seconds);
}

// Steps the capture and reports when it ends. `dt` is the frame's own duration.
void Engine::updateBenchmark(float dt) {
  if (benchmarkPending_ && world_) {
    benchmarkPending_ = false;
    benchmarkRunning_ = true;
    benchmarkElapsed_ = 0;
    camera_.setMotionEnabled(false);
    camera_.setOrientation(0.0f, 0.0f);
    benchmarkFrameMillis_.reserve(static_cast<std::size_t>(benchmarkSeconds_ * 120));
    LOGI("benchmark started: %u splats, one turn over %.0f s, gpu %.1f C", world_->count,
         benchmarkSeconds_, gpuTemperatureCelsius());
    return;  // the first frame after the pose change is not representative
  }
  if (!benchmarkRunning_) return;

  benchmarkElapsed_ += dt;
  benchmarkFrameMillis_.push_back(dt * 1000.0f);
  benchmarkGpuMillis_.push_back(static_cast<float>(frameLoop_->lastGpuMillis()));
  camera_.look(2.0f * static_cast<float>(M_PI) * dt / benchmarkSeconds_, 0.0f);
  if (benchmarkElapsed_ < benchmarkSeconds_) return;

  benchmarkRunning_ = false;
  std::vector<float>& f = benchmarkFrameMillis_;
  std::sort(f.begin(), f.end());
  const std::size_t n = f.size();
  if (n == 0) return;
  double total = 0;
  for (float ms : f) total += ms;
  const double mean = total / static_cast<double>(n);
  std::vector<float>& g = benchmarkGpuMillis_;
  std::sort(g.begin(), g.end());
  double gpuTotal = 0;
  for (float ms : g) gpuTotal += ms;
  const double gpuMean = gpuTotal / static_cast<double>(g.size());
  LOGI("benchmark: %zu frames, %.1f fps mean, frame ms mean %.1f p50 %.1f p95 %.1f max %.1f", n,
       1000.0 / mean, mean, f[n / 2], f[(n * 95) / 100], f[n - 1]);
  LOGI("benchmark gpu ms: mean %.1f p50 %.1f p95 %.1f max %.1f, gpu %.1f C at the end", gpuMean,
       g[g.size() / 2], g[(g.size() * 95) / 100], g.back(), gpuTemperatureCelsius());
}

// Every vsync steps the camera and the sorter, but the GPU only draws when something
// visible changed: a still scene costs no GPU time and almost no battery.
void Engine::render(int64_t frameTimeNanos) {
  if (!swapchain_) return;
  if (uploadPendingWorld()) redrawNeeded_ = true;

  std::optional<splat::Mat4> view;
  std::optional<splat::Mat4> proj;
  // Splats are drawn at the target's size when a render scale is set.
  const VkExtent2D extent = target_ ? target_->extent() : swapchain_->extent();
  if (world_) {
    const float dt = lastFrameNanos_ == 0 ? 0.0f : static_cast<float>(frameTimeNanos - lastFrameNanos_) * 1e-9f;
    lastFrameNanos_ = frameTimeNanos;
    const float clampedDt = std::min(dt, 0.1f);
    updateBenchmark(clampedDt);
    camera_.update(clampedDt);
    const splat::Vec3 position = camera_.position();
    view = camera_.viewMatrix();
    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    proj = splat::Mat4::perspective(65.0f * static_cast<float>(M_PI) / 180.0f, aspect, 0.05f, 200.0f);

    // Only splats inside a widened frustum reach the GPU, which pays per splat it
    // processes. The distance order itself does not depend on where the camera looks,
    // so moving costs a sort and turning only a cull of the order the sorter has.
    // Frames in between draw with whatever order they have.
    const splat::Mat4& v = *view;
    const splat::Vec3 forward{-v.at(2, 0), -v.at(2, 1), -v.at(2, 2)};
    const splat::Vec3 up{v.at(1, 0), v.at(1, 1), v.at(1, 2)};
    if (dt > 0.0f) {
      const float cosine = std::clamp(splat::dot(forward, lastFrameForward_), -1.0f, 1.0f);
      const float instant = std::acos(cosine) / dt;
      // Hold the peak for a few frames: a flick starts from rest.
      turnRate_ = std::max(instant, turnRate_ * 0.85f);
    }
    lastFrameForward_ = forward;
    statPose_[0].store(position.x, std::memory_order_relaxed);
    statPose_[1].store(position.y, std::memory_order_relaxed);
    statPose_[2].store(position.z, std::memory_order_relaxed);
    statPose_[3].store(camera_.yaw(), std::memory_order_relaxed);
    statPose_[4].store(camera_.pitch(), std::memory_order_relaxed);
    const bool moved = !lastSortedFrom_ ||
                       std::fabs(lastSortedFrom_->x - position.x) + std::fabs(lastSortedFrom_->y - position.y) +
                               std::fabs(lastSortedFrom_->z - position.z) > 0.005f;
    const bool turned = splat::dot(forward, lastSortedForward_) < kRecullCosine;
    if (moved || turned) {
      const float margin = std::min(kMaxCullMarginDegrees * kDegreesToRadians,
                                    cullMarginDegrees_ * kDegreesToRadians + turnRate_ * kCullStaleSeconds);
      // A pixel at unit depth: what a node may cover on screen before it is refined.
      splat::LodSettings lod;
      lod.budget = static_cast<std::size_t>(loadedBudget_);
      lod.pixelScaleLimit = 2.0f / (proj->at(1, 1) * static_cast<float>(extent.height));
      lod.view.forward = forward;
      sorter_->requestVisible(splat::Frustum::make(position, forward, up, 1.0f / proj->at(0, 0),
                                                   1.0f / proj->at(1, 1), margin), lod);
      lastSortedFrom_ = position;
      lastSortedForward_ = forward;
    }
    if (auto sorted = sorter_->take()) {
      lastSortMillis_ = sorted->sortMillis;
      lastCullMillis_ = sorted->cullMillis;
      lastSelectMillis_ = sorted->selectMillis;
      lastSelected_ = sorted->selected;
      pendingOrder_ = std::move(*sorted);
    }
    if (pendingOrder_ || benchmarkRunning_ || view->m != lastDrawnView_.m) redrawNeeded_ = true;
  }
  if (extent.width != lastDrawnExtent_.width || extent.height != lastDrawnExtent_.height) {
    redrawNeeded_ = true;
  }
  if (!redrawNeeded_) {
    publishStats(frameTimeNanos, false);
    return;
  }

  uint32_t imageIndex = 0;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  FrameLoop::Status status = frameLoop_->beginFrame(*swapchain_, imageIndex, cmd);
  if (status == FrameLoop::Status::swapchainOutOfDate) {
    recreateSwapchain();
    return;
  }
  if (status != FrameLoop::Status::ok) return;

  // Outside the render pass: transfers are not allowed inside one.
  if (world_ && pendingOrder_) {
    drawCount_ = static_cast<uint32_t>(pendingOrder_->order.size());
    splats_->updateOrder(cmd, frameLoop_->currentSlot(), *world_, pendingOrder_->order.data(), drawCount_);
    pendingOrder_.reset();
  }

  VkClearValue clear{};
  clear.color = {{0.05f, 0.05f, 0.08f, 1.0f}};
  VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  pass.renderPass = target_ ? target_->renderPass() : swapchain_->renderPass();
  pass.framebuffer = target_ ? target_->framebuffer() : swapchain_->framebuffer(imageIndex);
  pass.renderArea.extent = extent;
  pass.clearValueCount = 1;
  pass.pClearValues = &clear;
  vkCmdBeginRenderPass(cmd, &pass, VK_SUBPASS_CONTENTS_INLINE);

  // Negative height flips Y so that +Y is up, as in every other API we target.
  VkViewport viewport{0.0f, static_cast<float>(extent.height), static_cast<float>(extent.width),
                      -static_cast<float>(extent.height), 0.0f, 1.0f};
  VkRect2D scissor{{0, 0}, extent};
  vkCmdSetViewport(cmd, 0, 1, &viewport);
  vkCmdSetScissor(cmd, 0, 1, &scissor);

  if (world_) {
    splats_->draw(cmd, frameLoop_->currentSlot(), *world_, drawCount_, *view, *proj, camera_.position(), extent);
  } else {
    triangle_->draw(cmd);
  }

  vkCmdEndRenderPass(cmd);
  if (target_) target_->blitTo(cmd, swapchain_->image(imageIndex), swapchain_->extent());

  status = frameLoop_->endFrame(*swapchain_, imageIndex);
  redrawNeeded_ = false;
  lastDrawnView_ = view.value_or(splat::Mat4::identity());
  lastDrawnExtent_ = extent;
  if (status == FrameLoop::Status::swapchainOutOfDate ||
      (status == FrameLoop::Status::swapchainSuboptimal && surfaceExtentChanged())) {
    recreateSwapchain();
  }
  publishStats(frameTimeNanos, true);
}

void Engine::publishStats(int64_t frameTimeNanos, bool rendered) {
  if (rendered) ++fpsWindowFrames_;
  if (fpsWindowStart_ == 0) fpsWindowStart_ = frameTimeNanos;
  const int64_t elapsed = frameTimeNanos - fpsWindowStart_;
  if (elapsed >= 500'000'000LL) {
    const float fps = static_cast<float>(fpsWindowFrames_ * 1e9 / static_cast<double>(elapsed));
    statFps_.store(fps, std::memory_order_relaxed);
    statFrameMillis_.store(fps > 0.0f ? 1000.0f / fps : 0.0f, std::memory_order_relaxed);
    statGpuMillis_.store(static_cast<float>(frameLoop_->lastGpuMillis()), std::memory_order_relaxed);
    statSortMillis_.store(static_cast<float>(lastSortMillis_), std::memory_order_relaxed);
    statSplats_.store(world_ ? sourceCount_ : 0, std::memory_order_relaxed);
    statWalking_.store(camera_.hasCollider(), std::memory_order_relaxed);
    statMotion_.store(camera_.motionEnabled(), std::memory_order_relaxed);
    // An idle scene logs once, not every two seconds.
    const bool idle = fpsWindowFrames_ == 0;
    fpsWindowStart_ = frameTimeNanos;
    fpsWindowFrames_ = 0;
    if (++fpsWindowsSinceLog_ >= 4 && !(idle && lastLoggedIdle_)) {
      fpsWindowsSinceLog_ = 0;
      lastLoggedIdle_ = idle;
      const splat::Vec3 p = camera_.position();
      LOGI("%.1f fps, gpu %.1f ms, sort %.1f ms, cull %.1f ms, select %.1f ms, %u drawn of %zu selected of %u, pos %.2f %.2f %.2f, %s%s", fps,
           frameLoop_->lastGpuMillis(), lastSortMillis_, lastCullMillis_, lastSelectMillis_, drawCount_, lastSelected_,
           world_ ? world_->count : 0u, p.x, p.y, p.z,
           camera_.hasCollider() ? "walk" : "fly", camera_.motionEnabled() ? ", gyro" : "");
    }
  }
}

Engine::Stats Engine::stats() const {
  Stats s;
  s.fps = statFps_.load(std::memory_order_relaxed);
  s.frameMillis = statFrameMillis_.load(std::memory_order_relaxed);
  s.gpuMillis = statGpuMillis_.load(std::memory_order_relaxed);
  s.sortMillis = statSortMillis_.load(std::memory_order_relaxed);
  s.splatCount = statSplats_.load(std::memory_order_relaxed);
  s.walking = statWalking_.load(std::memory_order_relaxed);
  s.motion = statMotion_.load(std::memory_order_relaxed);
  return s;
}

}  // namespace splatkit
