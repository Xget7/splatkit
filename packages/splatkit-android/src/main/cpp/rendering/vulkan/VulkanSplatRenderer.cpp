#include "rendering/vulkan/VulkanSplatRenderer.h"

#include <algorithm>

#include <vulkan/vulkan_android.h>

#include "Log.h"

namespace splatkit {
namespace {

bool isSrgb(VkFormat format) {
  return format == VK_FORMAT_R8G8B8A8_SRGB || format == VK_FORMAT_B8G8R8A8_SRGB;
}

}  // namespace

VulkanSplatRenderer::VulkanSplatRenderer(VulkanContext& ctx, FrameLoop& frameLoop)
    : ctx_(ctx), frameLoop_(frameLoop) {}

VulkanSplatRenderer::~VulkanSplatRenderer() {
  setWindow(nullptr);
  ctx_.waitIdle();
  world_.reset();
}

void VulkanSplatRenderer::setWindow(ANativeWindow* window) {
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

void VulkanSplatRenderer::onSurfaceResized(uint32_t width, uint32_t height) {
  if (!swapchain_) return;
  const VkExtent2D current = swapchain_->extent();
  if (current.width == width && current.height == height) return;
  LOGI("surface resized to %ux%u, swapchain was %ux%u", width, height, current.width,
       current.height);
  keepSurfaceIf(recreateSwapchain());
}

void VulkanSplatRenderer::setRenderScale(float scale) {
  scale = std::clamp(scale, 0.1f, 2.0f);
  if (scale == renderScale_) return;
  renderScale_ = scale;
  if (!swapchain_) return;
  ctx_.waitIdle();
  if (!createRenderTarget()) {
    keepSurfaceIf(false);
    return;
  }
  if (pipelineFormat_ != activeFormat()) keepSurfaceIf(createPipelines());
}

void VulkanSplatRenderer::setLinearBlending(bool linear) {
  if (linear == linearBlending_) return;
  linearBlending_ = linear;
  if (swapchain_) keepSurfaceIf(recreateSwapchain());
}

void VulkanSplatRenderer::setVsync(bool vsync) {
  if (vsync == vsync_) return;
  vsync_ = vsync;
  if (swapchain_) keepSurfaceIf(recreateSwapchain());
}

VkExtent2D VulkanSplatRenderer::drawExtent() const {
  if (target_) return target_->extent();
  return swapchain_ ? swapchain_->extent() : VkExtent2D{0, 0};
}

bool VulkanSplatRenderer::uploadWorld(const splat::SplatCloud& cloud, int maxShDegree) {
  if (!splats_) return false;
  auto world = splats_->uploadWorld(cloud, maxShDegree);
  if (!world) return false;
  ctx_.waitIdle();  // the previous world may still be in flight
  world_ = std::move(world);
  splats_->bindWorld(*world_);
  return true;
}

bool VulkanSplatRenderer::draw(const Frame& frame) {
  if (!ready()) return false;
  uint32_t imageIndex = 0;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  FrameLoop::Status status = frameLoop_.beginFrame(*swapchain_, imageIndex, cmd);
  if (status == FrameLoop::Status::swapchainOutOfDate) {
    keepSurfaceIf(recreateSwapchain());
    return false;
  }
  if (status != FrameLoop::Status::ok) return false;

  const VkExtent2D extent = drawExtent();
  const uint32_t slot = frameLoop_.currentSlot();
  // Outside the render pass: transfers are not allowed inside one.
  if (world_ && frame.order != nullptr) {
    splats_->updateOrder(cmd, slot, *world_, frame.order, frame.orderCount);
  }

  VkClearValue clear{};
  clear.color = {{0.05f, 0.05f, 0.08f, 1.0f}};
  VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  pass.renderPass = activeRenderPass();
  pass.framebuffer = target_ ? target_->framebuffer() : swapchain_->framebuffer(imageIndex);
  pass.renderArea.extent = extent;
  pass.clearValueCount = 1;
  pass.pClearValues = &clear;
  vkCmdBeginRenderPass(cmd, &pass, VK_SUBPASS_CONTENTS_INLINE);

  // Negative height flips Y so that +Y is up, as in every other API we target.
  const VkViewport viewport{0.0f,
                            static_cast<float>(extent.height),
                            static_cast<float>(extent.width),
                            -static_cast<float>(extent.height),
                            0.0f,
                            1.0f};
  const VkRect2D scissor{{0, 0}, extent};
  vkCmdSetViewport(cmd, 0, 1, &viewport);
  vkCmdSetScissor(cmd, 0, 1, &scissor);

  if (world_) {
    splats_->draw(cmd, slot, *world_, std::min(frame.drawCount, world_->count),
                  std::min(frame.shDegree, world_->shDegree), frame.view, frame.proj,
                  frame.cameraPosition, extent);
  } else {
    triangle_->draw(cmd);
  }

  vkCmdEndRenderPass(cmd);
  if (target_) target_->blitTo(cmd, swapchain_->image(imageIndex), swapchain_->extent());

  status = frameLoop_.endFrame(*swapchain_, imageIndex);
  if (status == FrameLoop::Status::swapchainOutOfDate ||
      (status == FrameLoop::Status::swapchainSuboptimal && surfaceExtentChanged())) {
    keepSurfaceIf(recreateSwapchain());
  }
  return true;
}

bool VulkanSplatRenderer::createSurface() {
  VkAndroidSurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
  info.window = window_;
  if (vkCreateAndroidSurfaceKHR(ctx_.instance(), &info, nullptr, &surface_) != VK_SUCCESS) {
    LOGE("vkCreateAndroidSurfaceKHR failed");
    return false;
  }
  if (!ctx_.supportsPresent(surface_)) {
    LOGE("graphics queue cannot present to this surface");
    return false;
  }
  return true;
}

bool VulkanSplatRenderer::recreateSwapchain() {
  ++generation_;
  ctx_.waitIdle();
  const VkSwapchainKHR previous = swapchain_ ? swapchain_->release() : VK_NULL_HANDLE;
  swapchain_.reset();

  auto swapchain = Swapchain::create(ctx_, surface_, previous, vsync_, linearBlending_);
  if (previous != VK_NULL_HANDLE) vkDestroySwapchainKHR(ctx_.device(), previous, nullptr);
  if (!swapchain) {
    LOGE("%s", swapchain.error().message.c_str());
    return false;
  }
  swapchain_ = std::move(swapchain.value());
  if (!frameLoop_.onSwapchainCreated(*swapchain_)) return false;
  if (!createRenderTarget()) return false;

  // Viewport and scissor are dynamic, so a resize or rotation does not touch the
  // pipelines. A render pass with the same attachment format is compatible with the one
  // they were built against (Vulkan 1.1, 8.2 "Render Pass Compatibility"). Only a format
  // change, which also flips the sRGB output path, forces a rebuild.
  if (splats_ && triangle_ && pipelineFormat_ == activeFormat()) return true;
  return createPipelines();
}

bool VulkanSplatRenderer::createRenderTarget() {
  ++generation_;
  target_.reset();
  if (renderScale_ == 1.0f) return true;
  const VkExtent2D full = swapchain_->extent();
  const VkExtent2D scaled{std::max(1u, static_cast<uint32_t>(full.width * renderScale_)),
                          std::max(1u, static_cast<uint32_t>(full.height * renderScale_))};
  auto target = RenderTarget::create(ctx_, swapchain_->format(), scaled);
  if (!target) {
    LOGE("%s", target.error().message.c_str());
    return false;
  }
  target_ = std::move(target.value());
  return true;
}

bool VulkanSplatRenderer::createPipelines() {
  triangle_.reset();
  splats_.reset();
  pipelineFormat_ = activeFormat();
  const VkRenderPass pass = activeRenderPass();
  LOGI("pipelines for format %d, offscreen %d", static_cast<int>(pipelineFormat_), target_ ? 1 : 0);

  auto triangle = DebugTrianglePipeline::create(ctx_, pass);
  if (!triangle) {
    LOGE("%s", triangle.error().message.c_str());
    return false;
  }
  triangle_ = std::move(triangle.value());

  auto splats = SplatPipeline::create(ctx_, pass, isSrgb(pipelineFormat_));
  if (!splats) {
    LOGE("%s", splats.error().message.c_str());
    return false;
  }
  splats_ = std::move(splats.value());
  if (world_) splats_->bindWorld(*world_);
  return true;
}

// A rebuild that fails leaves no pipeline to draw with, so the surface is dropped and
// the view stays blank until the host attaches a surface again; the failure is logged.
void VulkanSplatRenderer::keepSurfaceIf(bool rebuilt) {
  if (rebuilt) return;
  LOGE("rendering stopped until the surface comes back");
  destroySurface();
}

// True when the surface no longer has the swapchain's size, which is how a rotation shows
// up when the driver only answers SUBOPTIMAL. A bare SUBOPTIMAL with the same size is the
// identity pre-transform being second best, and is not worth a rebuild.
bool VulkanSplatRenderer::surfaceExtentChanged() const {
  VkSurfaceCapabilitiesKHR caps{};
  if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx_.physicalDevice(), surface_, &caps) !=
      VK_SUCCESS) {
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

VkFormat VulkanSplatRenderer::activeFormat() const {
  return target_ ? target_->format() : swapchain_->format();
}

VkRenderPass VulkanSplatRenderer::activeRenderPass() const {
  return target_ ? target_->renderPass() : swapchain_->renderPass();
}

void VulkanSplatRenderer::destroySurface() {
  ctx_.waitIdle();
  triangle_.reset();
  splats_.reset();
  target_.reset();
  swapchain_.reset();
  if (surface_ != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(ctx_.instance(), surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
  }
}

}  // namespace splatkit
