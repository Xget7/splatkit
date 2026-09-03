#include "Engine.h"

#include <chrono>
#include <algorithm>
#include <cmath>
#include <optional>

#include <vulkan/vulkan_android.h>

#include "Log.h"
#include "splat/formats/GlbDecoder.h"
#include "splat/formats/SpzDecoder.h"
#include "splat/math/Mat4.h"

namespace splatkit {
namespace {

using Clock = std::chrono::steady_clock;

double millisSince(Clock::time_point t) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
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
  ctx_->waitIdle();
  VkSwapchainKHR previous = swapchain_ ? swapchain_->release() : VK_NULL_HANDLE;
  swapchain_.reset();

  auto sc = Swapchain::create(*ctx_, surface_, previous);
  if (previous != VK_NULL_HANDLE) vkDestroySwapchainKHR(ctx_->device(), previous, nullptr);
  if (!sc) {
    LOGE("%s", sc.error().message.c_str());
    return false;
  }
  swapchain_ = std::move(sc.value());
  if (!frameLoop_->onSwapchainCreated(*swapchain_)) return false;

  // Viewport and scissor are dynamic, so a resize or rotation does not touch the
  // pipelines. A render pass with the same attachment format is compatible with the one
  // they were built against (Vulkan 1.1, 8.2 "Render Pass Compatibility"). Only a format
  // change, which also flips the sRGB output path, forces a rebuild.
  if (splats_ && triangle_ && pipelineFormat_ == swapchain_->format()) return true;
  return createPipelines();
}

bool Engine::createPipelines() {
  triangle_.reset();
  splats_.reset();
  pipelineFormat_ = swapchain_->format();

  auto triangle = DebugTrianglePipeline::create(*ctx_, swapchain_->renderPass());
  if (!triangle) {
    LOGE("%s", triangle.error().message.c_str());
    return false;
  }
  triangle_ = std::move(triangle.value());

  auto splats = SplatPipeline::create(*ctx_, swapchain_->renderPass(), isSrgb(swapchain_->format()));
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
  swapchain_.reset();
  if (surface_ != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(ctx_->instance(), surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
  }
}

void Engine::loadWorld(const std::uint8_t* data, std::size_t size) {
  const auto start = Clock::now();
  auto decoded = splat::decodeSpz(data, size);
  if (!decoded) {
    LOGE("world decode failed: %s", decoded.error().message.c_str());
    return;
  }
  auto cloud = std::make_unique<splat::SplatCloud>(std::move(decoded.value()));
  LOGI("decoded %zu splats in %.0f ms, bounds y [%.2f, %.2f]", cloud->count(), millisSince(start),
       cloud->bounds.min[1], cloud->bounds.max[1]);
  std::lock_guard<std::mutex> lock(pendingMutex_);
  pendingCloud_ = std::move(cloud);
}

void Engine::loadCollider(const std::uint8_t* data, std::size_t size) {
  const auto start = Clock::now();
  auto decoded = splat::decodeGlb(data, size);
  if (!decoded) {
    LOGE("collider decode failed: %s", decoded.error().message.c_str());
    return;
  }
  auto collider = std::make_unique<splat::Collider>(decoded.value());
  LOGI("collider: %zu triangles, grid built in %.0f ms", collider->triangleCount(), millisSince(start));
  std::lock_guard<std::mutex> lock(pendingMutex_);
  pendingCollider_ = std::move(collider);
}

void Engine::uploadPendingWorld() {
  std::unique_ptr<splat::SplatCloud> cloud;
  std::unique_ptr<splat::Collider> collider;
  {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    cloud = std::move(pendingCloud_);
    collider = std::move(pendingCollider_);
  }
  if (collider) camera_.setCollider(std::move(collider));
  if (!cloud || !splats_) return;

  const auto start = Clock::now();
  auto world = splats_->uploadWorld(*cloud);
  if (!world) {
    LOGE("world upload failed");
    return;
  }
  ctx_->waitIdle();  // the previous world may still be in flight
  world_ = std::move(world);
  cloud_ = std::move(cloud);
  splats_->bindWorld(*world_);
  sorter_ = std::make_unique<splat::AsyncSorter>(cloud_->positions);
  lastSortedFrom_.reset();
  LOGI("uploaded %u splats in %.0f ms", world_->count, millisSince(start));
}

void Engine::render(int64_t frameTimeNanos) {
  if (!swapchain_) return;
  uploadPendingWorld();

  uint32_t imageIndex = 0;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  FrameLoop::Status status = frameLoop_->beginFrame(*swapchain_, imageIndex, cmd);
  if (status == FrameLoop::Status::swapchainOutOfDate) {
    recreateSwapchain();
    return;
  }
  if (status != FrameLoop::Status::ok) return;

  std::optional<splat::Mat4> view;
  std::optional<splat::Mat4> proj;
  VkExtent2D extent = swapchain_->extent();
  if (world_) {
    const float dt = lastFrameNanos_ == 0 ? 0.0f : static_cast<float>(frameTimeNanos - lastFrameNanos_) * 1e-9f;
    lastFrameNanos_ = frameTimeNanos;
    camera_.update(std::min(dt, 0.1f));
    const splat::Vec3 position = camera_.position();
    view = camera_.viewMatrix();
    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    proj = splat::Mat4::perspective(65.0f * static_cast<float>(M_PI) / 180.0f, aspect, 0.05f, 200.0f);

    // Ask for a new order when the camera moved; draw with whatever order we have.
    const bool moved = !lastSortedFrom_ ||
                       std::fabs(lastSortedFrom_->x - position.x) + std::fabs(lastSortedFrom_->y - position.y) +
                               std::fabs(lastSortedFrom_->z - position.z) > 0.005f;
    if (moved) {
      sorter_->request(position);
      lastSortedFrom_ = position;
    }
    // Outside the render pass: transfers are not allowed inside one.
    if (auto sorted = sorter_->take()) {
      lastSortMillis_ = sorted->millis;
      splats_->updateOrder(cmd, frameLoop_->currentSlot(), *world_, sorted->order.data());
    }
  }

  VkClearValue clear{};
  clear.color = {{0.05f, 0.05f, 0.08f, 1.0f}};
  VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  pass.renderPass = swapchain_->renderPass();
  pass.framebuffer = swapchain_->framebuffer(imageIndex);
  pass.renderArea.extent = swapchain_->extent();
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
    splats_->draw(cmd, frameLoop_->currentSlot(), *world_, *view, *proj, extent);
  } else {
    triangle_->draw(cmd);
  }

  vkCmdEndRenderPass(cmd);

  status = frameLoop_->endFrame(*swapchain_, imageIndex);
  if (status == FrameLoop::Status::swapchainOutOfDate ||
      (status == FrameLoop::Status::swapchainSuboptimal && surfaceExtentChanged())) {
    recreateSwapchain();
  }

  ++fpsWindowFrames_;
  if (fpsWindowStart_ == 0) fpsWindowStart_ = frameTimeNanos;
  const int64_t elapsed = frameTimeNanos - fpsWindowStart_;
  if (elapsed >= 2'000'000'000LL) {
    const splat::Vec3 p = camera_.position();
    LOGI("%.1f fps, sort %.1f ms, pos %.2f %.2f %.2f, %s%s", fpsWindowFrames_ * 1e9 / static_cast<double>(elapsed),
         lastSortMillis_, p.x, p.y, p.z, camera_.hasCollider() ? "walk" : "fly",
         camera_.motionEnabled() ? ", gyro" : "");
    fpsWindowStart_ = frameTimeNanos;
    fpsWindowFrames_ = 0;
  }
}

}  // namespace splatkit
