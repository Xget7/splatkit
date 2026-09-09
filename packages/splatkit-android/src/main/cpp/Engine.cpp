#include "Engine.h"

#include <chrono>
#include <cmath>
#include <utility>

#include "Log.h"
#include "splat/math/Frustum.h"

namespace splatkit {
namespace {

constexpr float kFieldOfViewRadians = 65.0f * static_cast<float>(M_PI) / 180.0f;
constexpr float kNearPlane = 0.05f;
constexpr float kFarPlane = 200.0f;
// A frame longer than this (a stall, a resume) steps the camera as if it were this long.
constexpr float kMaxFrameSeconds = 0.1f;

using Clock = std::chrono::steady_clock;

double millisSince(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
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
  engine->renderer_ = std::make_unique<SurfaceRenderer>(*engine->ctx_, *engine->frameLoop_);
  return engine;
}

Engine::~Engine() {
  renderer_.reset();
  sorter_.reset();
  if (ctx_) ctx_->waitIdle();
}

void Engine::setWindow(ANativeWindow* window) {
  renderer_->setWindow(window);
}

void Engine::onSurfaceResized(uint32_t width, uint32_t height) {
  renderer_->onSurfaceResized(width, height);
}

void Engine::setShDegree(int degree) {
  degree = std::clamp(degree, 0, kMaxShDegree);
  if (degree == shDegree_) return;
  shDegree_ = degree;
  redrawNeeded_ = true;
}

// Loading: decode on the calling thread, report, and leave the result for the frame.

void Engine::loadWorld(const std::uint8_t* data, std::size_t size) {
  reportWorld(loader_.loadWorld(data, size));
}

void Engine::loadWorldFile(const std::string& path) {
  reportWorld(loader_.loadWorldFile(path));
}

void Engine::loadCollider(const std::uint8_t* data, std::size_t size) {
  reportCollider(loader_.loadCollider(data, size));
}

void Engine::loadColliderFile(const std::string& path) {
  reportCollider(loader_.loadColliderFile(path));
}

void Engine::reportWorld(const splat::Result<splat::WorldLoader::WorldReport>& report) {
  if (!report) {
    LOGE("world load failed: %s", report.error().message.c_str());
    emit(Event::worldFailed, report.error().message);
    return;
  }
  const auto& r = report.value();
  LOGI("decoded %zu splats in %.0f ms, sh degree %d, bounds y [%.2f, %.2f], reordered in %.0f ms",
       r.splatCount, r.decodeMillis, r.shDegree, r.bounds.min[1], r.bounds.max[1], r.reorderMillis);
  if (r.nodeCount > 0) {
    LOGI("level of detail tree: %zu nodes over %zu splats, built in %.0f ms", r.nodeCount,
         r.splatCount, r.treeMillis);
  }
}

void Engine::reportCollider(const splat::Result<splat::WorldLoader::ColliderReport>& report) {
  if (!report) {
    LOGE("collider load failed: %s", report.error().message.c_str());
    emit(Event::colliderFailed, report.error().message);
    return;
  }
  LOGI("collider: %zu triangles, grid built in %.0f ms", report.value().triangleCount,
       report.value().millis);
}

// Uploads what the loader left. True when a new world is drawn from now on.
bool Engine::applyPendingLoads() {
  if (auto collider = loader_.takeCollider()) {
    camera_.setCollider(std::move(collider));
    emit(Event::colliderReady);
  }
  auto world = loader_.takeWorld();
  if (!world) return false;

  const auto start = Clock::now();
  if (!renderer_->uploadWorld(world->splats(), maxShDegree_.load())) {
    LOGE("world upload failed");
    emit(Event::worldFailed, "GPU upload failed");
    return false;
  }
  // The sorter keeps the positions, or the tree, whose attributes are already on the GPU.
  sorter_ = world->tree ? std::make_unique<splat::AsyncSorter>(world->tree)
                        : std::make_unique<splat::AsyncSorter>(std::move(world->cloud->positions));
  sourceCount_ = static_cast<uint32_t>(world->sourceCount);
  loadedBudget_ = world->budget;
  planner_.invalidate();
  pendingOrder_.reset();  // an order for the old world indexes past a smaller new one
  drawCount_ = 0;         // the first frustum sort decides what is visible
  const GpuWorld& gpu = *renderer_->world();
  LOGI("uploaded %u splats in %.0f ms, sh degree %d", gpu.count, millisSince(start), gpu.shDegree);
  emit(Event::worldReady, {}, sourceCount_);
  return true;
}

// Camera.

void Engine::setCameraPose(const CameraPose& pose) {
  camera_.setPosition({pose.x, pose.y, pose.z});
  camera_.setOrientation(pose.yaw, pose.pitch);
  planner_.invalidate();  // a teleport needs a fresh sort, not a cull
  redrawNeeded_ = true;
  // Published now, not at the next frame: a host that sets and reads back before a
  // world exists would otherwise see the previous pose.
  publishPose();
}

void Engine::publishPose() {
  stats_.publishPose(camera_.position(), camera_.yaw(), camera_.pitch());
}

Engine::FrameCamera Engine::frameCamera(VkExtent2D extent) const {
  FrameCamera c;
  c.view = camera_.viewMatrix();
  const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
  c.proj = splat::Mat4::perspective(kFieldOfViewRadians, aspect, kNearPlane, kFarPlane);
  c.axes.position = camera_.position();
  c.axes.forward = {-c.view.at(2, 0), -c.view.at(2, 1), -c.view.at(2, 2)};
  c.axes.up = {c.view.at(1, 0), c.view.at(1, 1), c.view.at(1, 2)};
  c.axes.tanHalfX = 1.0f / c.proj.at(0, 0);
  c.axes.tanHalfY = 1.0f / c.proj.at(1, 1);
  return c;
}

// Visibility: only the splats inside a widened frustum reach the GPU, which pays per
// splat it processes. The planner says when the view changed enough to ask again.

void Engine::requestVisible(const FrameCamera& camera, float dt, VkExtent2D extent) {
  auto frustum = planner_.update(camera.axes, dt, lastSort_.cullMillis);
  if (!frustum) return;
  splat::LodSettings lod;
  lod.budget = static_cast<std::size_t>(loadedBudget_);
  // A pixel at unit depth: what a node may cover on screen before it is refined.
  lod.pixelScaleLimit = 2.0f / (camera.proj.at(1, 1) * static_cast<float>(extent.height));
  lod.view.forward = camera.axes.forward;
  sorter_->requestVisible(*frustum, lod);
}

void Engine::takeSortResult() {
  auto sorted = sorter_->take();
  if (!sorted) return;
  lastSort_.sortMillis = sorted->sortMillis;
  lastSort_.cullMillis = sorted->cullMillis;
  lastSort_.selectMillis = sorted->selectMillis;
  lastSort_.selected = sorted->selected;
  pendingOrder_ = std::move(*sorted);
}

// Benchmark and stats.

void Engine::startBenchmark(float seconds) {
  benchmark_.start(seconds);
  renderer_->setVsync(false);  // so frame times are not vsync multiples
}

void Engine::driveBenchmark(float dt, const GpuWorld& world) {
  if (benchmark_.pending()) {
    camera_.setMotionEnabled(false);
    camera_.setOrientation(0.0f, 0.0f);
    benchmark_.begin(world.count);
    return;
  }
  if (benchmark_.running()) camera_.look(benchmark_.step(dt, frameLoop_->lastGpuMillis()), 0.0f);
}

StatsPublisher::Sample Engine::sample() const {
  StatsPublisher::Sample s;
  s.gpuMillis = frameLoop_->lastGpuMillis();
  s.sortMillis = lastSort_.sortMillis;
  s.cullMillis = lastSort_.cullMillis;
  s.selectMillis = lastSort_.selectMillis;
  s.selected = lastSort_.selected;
  s.drawn = drawCount_;
  const GpuWorld* world = renderer_->world();
  s.sourceSplats = world ? sourceCount_ : 0;
  s.gpuSplats = world ? world->count : 0;
  s.walking = camera_.hasCollider();
  s.motion = camera_.motionEnabled();
  return s;
}

// The frame.

float Engine::frameSeconds(int64_t frameTimeNanos) {
  const float dt =
      lastFrameNanos_ == 0 ? 0.0f : static_cast<float>(frameTimeNanos - lastFrameNanos_) * 1e-9f;
  lastFrameNanos_ = frameTimeNanos;
  return std::min(dt, kMaxFrameSeconds);
}

// Every vsync steps the camera and the sorter, but the GPU only draws when something
// visible changed: a still scene costs no GPU time and almost no battery.
void Engine::render(int64_t frameTimeNanos) {
  if (!renderer_->ready()) return;
  if (applyPendingLoads()) redrawNeeded_ = true;

  const VkExtent2D extent = renderer_->drawExtent();
  const GpuWorld* world = renderer_->world();
  std::optional<FrameCamera> camera;
  if (world != nullptr) {
    const float dt = frameSeconds(frameTimeNanos);
    driveBenchmark(dt, *world);
    camera_.update(dt);
    camera = frameCamera(extent);
    publishPose();
    requestVisible(*camera, dt, extent);
    takeSortResult();
    if (pendingOrder_ || benchmark_.running() || camera->view.m != lastDrawnView_.m) {
      redrawNeeded_ = true;
    }
  }
  const uint32_t generation = renderer_->generation();
  if (generation != lastDrawnGeneration_) redrawNeeded_ = true;
  const auto sampler = [this] { return sample(); };
  if (!redrawNeeded_) {
    stats_.onFrame(frameTimeNanos, false, sampler);
    return;
  }

  SurfaceRenderer::Frame frame;
  if (camera) {
    if (pendingOrder_) {
      drawCount_ = static_cast<uint32_t>(pendingOrder_->order.size());
      frame.order = pendingOrder_->order.data();
      frame.orderCount = drawCount_;
    }
    frame.drawCount = drawCount_;
    frame.shDegree = shDegree_;
    frame.view = camera->view;
    frame.proj = camera->proj;
    frame.cameraPosition = camera->axes.position;
  }
  if (!renderer_->draw(frame)) return;  // the order and the redraw wait for the next frame
  pendingOrder_.reset();
  redrawNeeded_ = false;
  lastDrawnView_ = camera ? camera->view : splat::Mat4::identity();
  lastDrawnGeneration_ = generation;
  stats_.onFrame(frameTimeNanos, true, sampler);
}

}  // namespace splatkit
