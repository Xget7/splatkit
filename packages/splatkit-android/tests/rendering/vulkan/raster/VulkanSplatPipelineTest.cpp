#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "rendering/vulkan/frame/VulkanCameraUniforms.h"
#include "rendering/vulkan/presentation/VulkanRenderTarget.h"
#include "rendering/vulkan/raster/VulkanSplatPipeline.h"
#include "rendering/vulkan/world/VulkanWorld.h"
#include "support/VulkanTestContext.h"

namespace {
using splatkit::VulkanBuffer;
using splatkit::VulkanCameraUniforms;
using splatkit::VulkanRenderTarget;
using splatkit::VulkanSplatPipeline;
using splatkit::VulkanWorld;
namespace test = splatkit::test;
using test::require;
using Gpu = test::VulkanTestContext;

constexpr VkExtent2D kExtent{64, 64};
constexpr VkFormat kFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr float kDepth = 5;
constexpr float kSigmaPixels = 8;
// The rasterizer's low-pass filter adds this to the projected variance (splat.vert).
constexpr float kLowPassVariance = 0.3f;
// Pixels this close to the quad's edge may fall either side of it.
constexpr float kEdgePixels = 1.5f;
// Absolute error, in 8-bit steps, allowed per pixel: rounding plus half-precision shading.
constexpr int kTolerance = 2;

struct Scene {
  std::unique_ptr<VulkanCameraUniforms> cameras;
  std::unique_ptr<VulkanRenderTarget> target;
  std::unique_ptr<VulkanSplatPipeline> pipeline;
  splat::Mat4 proj = splat::Mat4::perspective(1, 1, 0.1f, 100);
};

Scene scene(const Gpu& gpu) {
  Scene s;
  auto cameras = VulkanCameraUniforms::create(*gpu.context);
  require(static_cast<bool>(cameras), "camera uniforms");
  s.cameras = std::move(cameras.value());
  auto target = VulkanRenderTarget::create(*gpu.context, kFormat, kExtent);
  require(static_cast<bool>(target), "offscreen target");
  s.target = std::move(target.value());
  auto pipeline = VulkanSplatPipeline::create(*gpu.context, s.target->renderPass(), *s.cameras);
  require(static_cast<bool>(pipeline), "splat pipeline");
  s.pipeline = std::move(pipeline.value());
  return s;
}

// The red channel of one white splat at the centre of the view, over transparent black.
std::vector<uint8_t> render(const Gpu& gpu, const Scene& s, float alpha) {
  const float worldSigma = kSigmaPixels * kDepth / (kExtent.width * s.proj.at(0, 0) / 2);
  const float variance = worldSigma * worldSigma;
  splat::SplatCloud cloud;
  cloud.positions = {0, 0, -kDepth};
  cloud.covariances = {variance, 0, 0, variance, 0, variance};
  cloud.colors = {1, 1, 1};
  cloud.alphas = {alpha};
  auto world = VulkanWorld::upload(*gpu.context, cloud, 0, true);
  require(world != nullptr, "world upload");
  s.pipeline->bindWorld(*world);
  s.cameras->update(0, splat::Mat4::identity(), s.proj, {0, 0, 0}, kExtent, false, 0);

  const VkDeviceSize bytes = VkDeviceSize{kExtent.width} * kExtent.height * 4;
  auto pixels = VulkanBuffer::hostVisible(*gpu.context, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  require(pixels && pixels->mapped(), "pixel readback buffer");
  const uint32_t order = 0;
  gpu.submit([&](VkCommandBuffer cmd) {
    world->recordOrderUpload(cmd, 0, &order, 1);
    const VkClearValue clear{};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = s.target->renderPass();
    pass.framebuffer = s.target->framebuffer();
    pass.renderArea.extent = kExtent;
    pass.clearValueCount = 1;
    pass.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &pass, VK_SUBPASS_CONTENTS_INLINE);
    const auto width = static_cast<float>(kExtent.width);
    const auto height = static_cast<float>(kExtent.height);
    const VkViewport viewport{0, height, width, -height, 0, 1};
    const VkRect2D scissor{{0, 0}, kExtent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    s.pipeline->draw(cmd, 0, *world, 1, 0);
    vkCmdEndRenderPass(cmd);
    VkMemoryBarrier written{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    written.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    written.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &written, 0, nullptr, 0, nullptr);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {kExtent.width, kExtent.height, 1};
    vkCmdCopyImageToBuffer(cmd, s.target->image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           pixels->handle(), 1, &copy);
    VkMemoryBarrier copied{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    copied.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    copied.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
                         &copied, 0, nullptr, 0, nullptr);
  });
  pixels->invalidate(0, bytes);
  const auto* rgba = static_cast<const uint8_t*>(pixels->mapped());
  std::vector<uint8_t> red(kExtent.width * kExtent.height);
  for (size_t i = 0; i < red.size(); ++i) red[i] = rgba[i * 4];
  return red;
}

// Compares every pixel with alpha * exp(-r^2 / 2), cut at the quad's half-width `radius`
// sigma and at the fragment's 1/255 discard. Pixels on the quad's edge are skipped.
void requireGaussian(const std::vector<uint8_t>& red, float alpha, float radius) {
  const float sigma = std::sqrt(kSigmaPixels * kSigmaPixels + kLowPassVariance);
  const float half = radius * sigma;
  int compared = 0;
  for (uint32_t y = 0; y < kExtent.height; ++y) {
    for (uint32_t x = 0; x < kExtent.width; ++x) {
      const float dx = x + 0.5f - kExtent.width / 2.0f;
      const float dy = y + 0.5f - kExtent.height / 2.0f;
      const float edge = std::max(std::abs(dx), std::abs(dy));
      if (std::abs(edge - half) < kEdgePixels) continue;
      float expected = 0;
      if (edge < half) {
        expected = std::min(alpha * std::exp(-0.5f * (dx * dx + dy * dy) / (sigma * sigma)), 1.0f);
        if (expected < 1.0f / 255.0f) expected = 0;
      }
      const int got = red[y * kExtent.width + x];
      if (std::abs(got - static_cast<int>(std::lround(expected * 255))) > kTolerance) {
        std::fprintf(stderr, "pixel %u,%u: got %d, expected %.2f\n", x, y, got, expected * 255);
        require(false, "pixel matches the CPU Gaussian");
      }
      ++compared;
    }
  }
  require(compared > 0, "pixels compared");
}

void testTranslucent(const Gpu& gpu, const Scene& s) {
  // Peak 0.8 reaches the 2/255 cutoff past 2.5 sigma, so the quad stops at 2.5 sigma.
  requireGaussian(render(gpu, s, 0.8f), 0.8f, 2.5f);
  std::puts("PASS translucent splat matches the Gaussian out to 2.5 sigma");
}

void testLodCore(const Gpu& gpu, const Scene& s) {
  // An LOD opacity of 3 is solid in its core and meets the 2/255 cutoff at 3.45 sigma.
  const float alpha = 3;
  requireGaussian(render(gpu, s, alpha), alpha, std::sqrt(2 * std::log(alpha * 255 / 2)));
  std::puts("PASS LOD splat saturates in its core and fades to its quad edge");
}
}  // namespace

int main() {
  try {
    Gpu gpu;
    std::printf("GPU: %s\n", gpu.context->deviceDescription().c_str());
    const Scene s = scene(gpu);
    testTranslucent(gpu, s);
    testLodCore(gpu, s);
    gpu.requireValidationClean();
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
}
