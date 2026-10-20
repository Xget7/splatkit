#include "rendering/vulkan/VulkanSplatRenderer.h"

#include <algorithm>
#include <limits>

#include "engine/TraceSection.h"
#include "rendering/vulkan/sort/VulkanRadixSort.h"

#include "splatkit/Log.h"

namespace splatkit {
namespace {

// The residency range reported to the adapters.
constexpr uint32_t kMinResidencyCapacity = 100'000;
constexpr uint32_t kMaxResidencyCapacity = 8'000'000;

bool isSrgb(VkFormat format) {
  return format == VK_FORMAT_R8G8B8A8_SRGB || format == VK_FORMAT_B8G8R8A8_SRGB;
}

}  // namespace

VulkanSplatRenderer::VulkanSplatRenderer(VulkanContext& ctx, VulkanFrameLoop& frameLoop)
    : ctx_(ctx), frameLoop_(frameLoop), surface_(ctx, frameLoop, [this] { splats_.reset(); }) {}

VulkanSplatRenderer::~VulkanSplatRenderer() {
  surface_.attach(nullptr);
  ctx_.waitIdle();
  world_.reset();
}

void VulkanSplatRenderer::setWindow(ANativeWindow* window) {
  if (surface_.attach(window)) rebuildPipelinesIfNeeded();
}

void VulkanSplatRenderer::onSurfaceResized(uint32_t width, uint32_t height) {
  if (surface_.resize(width, height)) rebuildPipelinesIfNeeded();
}

void VulkanSplatRenderer::setRenderScale(float scale) {
  if (surface_.setRenderScale(scale)) rebuildPipelinesIfNeeded();
}

void VulkanSplatRenderer::setLinearBlending(bool linear) {
  if (surface_.setLinearBlending(linear)) rebuildPipelinesIfNeeded();
}

void VulkanSplatRenderer::setVsync(bool vsync) {
  if (surface_.setVsync(vsync)) rebuildPipelinesIfNeeded();
}

DeviceCapabilities VulkanSplatRenderer::deviceCapabilities() const {
  DeviceCapabilities caps;
  // GpuLOD clamps a hierarchy at 2.2M nodes; the Android adapter caps residency at 8M.
  caps.limits.maxLodCapacitySplats = VulkanLodSelection::kMaxBudget;
  caps.limits.minResidencyCapacitySplats = kMinResidencyCapacity;
  caps.limits.maxResidencyCapacitySplats = kMaxResidencyCapacity;
  // Vulkan has no screen-tile rasterization and conservative occlusion is unimplemented.
  caps.supportsComputeTiles = false;
  caps.supportsHiZOcclusion = false;
  const bool gpu = selectsLodOnGpu();
  caps.supportsSubgroups = gpu;
  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(ctx_.physicalDevice(), &properties);
  caps.maxTextureDimension = properties.limits.maxImageDimension2D;
  RenderPolicySupport& policy = caps.policy;
  policy.fallback.raster = RasterStrategy::hardware;
  policy.fallback.tileSize = 16;
  policy.fallback.lodErrorPixels = 1.0f;
  policy.fallback.alphaThreshold = 1.0f / 255.0f;
  policy.fallback.subpixelThreshold = 0.5f;
  policy.fallback.sortDepth = SortKeyBits::full32;
  policy.fallback.enableFrustumCulling = true;
  policy.fallback.enableEarlyTermination = true;
  // The GPU visibility/sort path applies the sub-pixel cutoff and the key width per
  // instance; without it the CPU path has neither, and the rest is not implemented.
  policy.sortDepth = gpu;
  policy.subpixelThreshold = gpu;
  return caps;
}

bool VulkanSplatRenderer::applyRenderPolicy(const RenderPolicy& policy, std::string* reason) {
  if (policy.sortDepth == policy_.sortDepth &&
      policy.subpixelThreshold == policy_.subpixelThreshold) {
    return true;
  }
  const RenderPolicy previous = policy_;
  policy_ = policy;
  if (compute_ != nullptr) {
    ctx_.waitIdle();
    if (!compute_->applyRenderPolicy(policy_, reason)) {
      policy_ = previous;
      std::string ignored;
      compute_->applyRenderPolicy(previous, &ignored);
      return false;
    }
  }
  return true;
}

// Applies the stored policy to a freshly created frame compute, before any frame uses it.
void VulkanSplatRenderer::applyPolicyToCompute() {
  if (compute_ == nullptr) return;
  std::string reason;
  if (!compute_->applyRenderPolicy(policy_, &reason)) {
    LOGW("Vulkan policy on the new compute pass failed: %s", reason.c_str());
  }
}

Extent VulkanSplatRenderer::drawExtent() const {
  const VkExtent2D extent = surface_.drawExtent();
  return {extent.width, extent.height};
}

std::optional<GpuWorldInfo> VulkanSplatRenderer::world() const {
  if (!world_) return std::nullopt;
  return GpuWorldInfo{world_->count, world_->shDegree};
}

bool VulkanSplatRenderer::uploadWorld(const splat::SplatCloud& cloud, int maxShDegree) {
  const TraceSection section(trace::kUploadWorld);
  if (!splats_ || cloud.count() > std::numeric_limits<uint32_t>::max()) return false;
  ctx_.waitIdle();
  auto compute = VulkanFrameCompute::create(ctx_, static_cast<uint32_t>(cloud.count()));
  // sortsOnGpu() promised GPU order, so a world without its compute passes would never draw.
  if (!compute) {
    LOGE("GPU upload rejected: %s", compute.error().message.c_str());
    return false;
  }
  auto world = VulkanWorld::upload(ctx_, cloud, maxShDegree, false);
  if (!world) return false;
  ctx_.waitIdle();  // the previous world may still be in flight
  world_ = std::move(world);
  compute_ = std::move(compute.value());
  applyPolicyToCompute();
  splats_->bindWorld(*world_);
  worldFrameCompletion_.reset();
  return true;
}

bool VulkanSplatRenderer::sortsOnGpu() const {
  return VulkanVisibilityPass::queryCapabilities(ctx_).supported &&
         VulkanRadixSort::queryCapabilities(ctx_).supported;
}

bool VulkanSplatRenderer::selectsLodOnGpu() const {
  return sortsOnGpu();
}

bool VulkanSplatRenderer::uploadLodWorld(const splat::LodTree& tree, int maxShDegree,
                                         uint32_t budget) {
  const TraceSection section(trace::kUploadWorld);
  if (!splats_ || tree.nodeCount() > std::numeric_limits<uint32_t>::max()) return false;
  ctx_.waitIdle();
  auto compute =
      VulkanFrameCompute::create(ctx_, static_cast<uint32_t>(tree.nodeCount()), &tree, budget);
  if (!compute) {
    LOGE("GPU LOD upload rejected: %s", compute.error().message.c_str());
    return false;
  }
  auto world = VulkanWorld::upload(ctx_, tree.nodes, maxShDegree, false);
  if (!world) return false;
  world_ = std::move(world);
  compute_ = std::move(compute.value());
  applyPolicyToCompute();
  splats_->bindWorld(*world_);
  worldFrameCompletion_.reset();
  return true;
}

bool VulkanSplatRenderer::createSlab(uint32_t capacity, int shDegree) {
  const TraceSection section(trace::kUploadWorld);
  if (!splats_) return false;
  ctx_.waitIdle();
  auto compute = VulkanFrameCompute::create(ctx_, capacity);
  if (!compute) {
    LOGE("GPU slab rejected: %s", compute.error().message.c_str());
    return false;
  }
  auto world = VulkanWorld::createSlab(ctx_, capacity, shDegree, false);
  if (!world) return false;
  ctx_.waitIdle();  // the previous world may still be in flight
  world_ = std::move(world);
  compute_ = std::move(compute.value());
  applyPolicyToCompute();
  splats_->bindWorld(*world_);
  worldFrameCompletion_.reset();
  return true;
}

bool VulkanSplatRenderer::uploadTile(uint32_t offset, const splat::SplatCloud& cloud) {
  const TraceSection section(trace::kUploadTile);
  if (!splats_ || !world_) return false;
  return world_->uploadTile(offset, cloud);
}

bool VulkanSplatRenderer::draw(const Frame& frame) {
  if (!ready()) return false;
  uint32_t imageIndex = 0;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  VulkanFrameLoop::Status status = [&] {
    const TraceSection section(trace::kWaitForFrameSlot);
    return frameLoop_.beginFrame(surface_.swapchain(), imageIndex, cmd);
  }();
  if (status == VulkanFrameLoop::Status::swapchainOutOfDate) {
    rebuildSurface(status);
    return false;
  }
  if (status != VulkanFrameLoop::Status::ok) return false;

  const VkExtent2D extent = surface_.drawExtent();
  const uint32_t slot = frameLoop_.currentSlot();
  const bool outputLinear = isSrgb(pipelineFormat_);
  // Outside the render pass: transfers are not allowed inside one.
  std::optional<VulkanFrameCompute::Draw> gpuDraw;
  if (world_ && compute_ && frame.orderSource == OrderSource::gpu) {
    const VkBuffer camera =
        camera_->update(slot, frame.view, frame.proj, frame.cameraPosition, extent, outputLinear);
    {
      const TraceSection section(trace::kEncodeCompute);
      gpuDraw = compute_->encode(cmd, slot, camera, *world_->splats, frame);
    }
    if (gpuDraw)
      splats_->bindOrder(slot, gpuDraw->order, gpuDraw->capacity);
    else
      LOGE("GPU frame encode failed; submitting clear frame to preserve fence lifecycle");
  } else if (world_ && !compute_ && frame.order != nullptr) {
    world_->recordOrderUpload(cmd, slot, frame.order, frame.orderCount);
  }

  VkClearValue clear{};
  clear.color = {{0.05f, 0.05f, 0.08f, 1.0f}};
  VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  pass.renderPass = surface_.activeRenderPass();
  pass.framebuffer = surface_.framebuffer(imageIndex);
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

  if (world_ && gpuDraw) {
    splats_->drawIndirect(cmd, slot, *world_, frame.shDegree, gpuDraw->arguments);
  } else if (world_ && !compute_ && frame.orderSource == OrderSource::cpu) {
    const uint32_t count = std::min(frame.drawCount, world_->count);
    if (count > 0) {
      camera_->update(slot, frame.view, frame.proj, frame.cameraPosition, extent, outputLinear);
    }
    splats_->draw(cmd, slot, *world_, count, std::min(frame.shDegree, world_->shDegree));
  }

  vkCmdEndRenderPass(cmd);
  surface_.blitIfScaled(cmd, imageIndex);

  const uint64_t submittedBefore = frameLoop_.lastSubmission();
  {
    const TraceSection section(trace::kSubmitPresent);
    status = frameLoop_.endFrame(surface_.swapchain(), imageIndex);
  }
  if (gpuDraw && frameLoop_.lastSubmission() != submittedBefore)
    compute_->submitted(slot, frameLoop_.lastSubmission());
  const bool drewWorld =
      world_ && (gpuDraw || (!compute_ && frame.orderSource == OrderSource::cpu &&
                             std::min(frame.drawCount, world_->count) > 0));
  if (drewWorld && (status == VulkanFrameLoop::Status::ok ||
                    status == VulkanFrameLoop::Status::swapchainSuboptimal)) {
    worldFrameCompletion_.submitted(frameLoop_.lastSubmission());
  }
  rebuildSurface(status);
  return true;
}

void VulkanSplatRenderer::collectCompletedFrames() {
  const uint64_t completed = frameLoop_.completedSubmission();
  if (compute_) compute_->collectCompleted(completed);
}

bool VulkanSplatRenderer::createPipelines() {
  splats_.reset();
  pipelineFormat_ = surface_.activeFormat();
  const VkRenderPass pass = surface_.activeRenderPass();
  LOGI("pipelines for format %d", static_cast<int>(pipelineFormat_));

  if (!camera_) {
    auto camera = VulkanCameraUniforms::create(ctx_);
    if (!camera) {
      LOGE("%s", camera.error().message.c_str());
      return false;
    }
    camera_ = std::move(camera.value());
  }
  auto splats = VulkanSplatPipeline::create(ctx_, pass, *camera_);
  if (!splats) {
    LOGE("%s", splats.error().message.c_str());
    return false;
  }
  splats_ = std::move(splats.value());
  if (world_) splats_->bindWorld(*world_);
  return true;
}

// After a surface call that left a swapchain up: only a changed format, or a surface that
// just came back (teardown cleared `splats_`), needs new pipelines. A failed build leaves
// no pipeline to draw with, so the surface is dropped and the view stays blank until the
// host attaches a surface again; the failure is logged.
void VulkanSplatRenderer::rebuildPipelinesIfNeeded() {
  if (!surface_.ready()) return;
  if (splats_ && pipelineFormat_ == surface_.activeFormat()) return;
  if (!createPipelines()) surface_.drop();
}

// Runs every frame; a few comparisons when nothing was rebuilt.
void VulkanSplatRenderer::rebuildSurface(VulkanFrameLoop::Status status) {
  if (surface_.rebuildIfChanged(status)) rebuildPipelinesIfNeeded();
}

}  // namespace splatkit
