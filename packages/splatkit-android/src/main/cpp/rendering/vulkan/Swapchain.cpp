#include "rendering/vulkan/Swapchain.h"

#include "Log.h"

namespace splatkit {

splat::Result<std::unique_ptr<Swapchain>> Swapchain::create(const VulkanContext& ctx,
                                                            VkSurfaceKHR surface,
                                                            VkSwapchainKHR previous) {
  std::unique_ptr<Swapchain> sc(new Swapchain(ctx));

  vkb::SwapchainBuilder builder(ctx.physicalDevice(), ctx.device(), surface,
                                ctx.queueFamily(), ctx.queueFamily());
  auto result = builder
                    .set_desired_format({VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
                    .add_fallback_format({VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
                    .add_fallback_format({VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
                    .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
                    .set_desired_min_image_count(3)
                    .set_old_swapchain(previous)
                    .set_pre_transform_flags(VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
                    .build();
  if (!result) {
    return splat::Error{splat::ErrorCode::gpuUnavailable,
                        "swapchain: " + result.error().message()};
  }
  sc->swapchain_ = result.value();

  auto views = sc->swapchain_.get_image_views();
  if (!views) {
    return splat::Error{splat::ErrorCode::gpuUnavailable, "swapchain image views"};
  }
  sc->imageViews_ = views.value();

  if (auto r = sc->createRenderPass(); !r) return r.error();
  if (auto r = sc->createFramebuffers(); !r) return r.error();

  LOGI("Swapchain %ux%u, %u images, format %d", sc->extent().width, sc->extent().height,
       sc->imageCount(), sc->format());
  return sc;
}

Swapchain::~Swapchain() {
  VkDevice device = ctx_.device();
  for (VkFramebuffer fb : framebuffers_) vkDestroyFramebuffer(device, fb, nullptr);
  if (renderPass_ != VK_NULL_HANDLE) vkDestroyRenderPass(device, renderPass_, nullptr);
  swapchain_.destroy_image_views(imageViews_);
  vkb::destroy_swapchain(swapchain_);
}

VkSwapchainKHR Swapchain::release() {
  VkSwapchainKHR handle = swapchain_.swapchain;
  swapchain_.swapchain = VK_NULL_HANDLE;
  return handle;
}

// One color attachment, cleared at the start, ready for presentation at the end.
// No depth: splats are blended in sorted order, never depth tested.
splat::Result<splat::Ok> Swapchain::createRenderPass() {
  VkAttachmentDescription color{};
  color.format = format();
  color.samples = VK_SAMPLE_COUNT_1_BIT;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorRef;

  // The acquire semaphore is waited at the color output stage, so the pass must not
  // write the image before that stage: this dependency says exactly that.
  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.srcAccessMask = 0;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  info.attachmentCount = 1;
  info.pAttachments = &color;
  info.subpassCount = 1;
  info.pSubpasses = &subpass;
  info.dependencyCount = 1;
  info.pDependencies = &dependency;

  if (vkCreateRenderPass(ctx_.device(), &info, nullptr, &renderPass_) != VK_SUCCESS) {
    return splat::Error{splat::ErrorCode::gpuUnavailable, "render pass"};
  }
  return splat::Ok{};
}

splat::Result<splat::Ok> Swapchain::createFramebuffers() {
  framebuffers_.resize(imageViews_.size(), VK_NULL_HANDLE);
  for (size_t i = 0; i < imageViews_.size(); ++i) {
    VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    info.renderPass = renderPass_;
    info.attachmentCount = 1;
    info.pAttachments = &imageViews_[i];
    info.width = extent().width;
    info.height = extent().height;
    info.layers = 1;
    if (vkCreateFramebuffer(ctx_.device(), &info, nullptr, &framebuffers_[i]) != VK_SUCCESS) {
      return splat::Error{splat::ErrorCode::gpuUnavailable, "framebuffer"};
    }
  }
  return splat::Ok{};
}

}  // namespace splatkit
