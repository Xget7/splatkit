#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include <vulkan/vulkan.h>

#include "rendering/vulkan/device/VulkanBuffer.h"
#include "rendering/vulkan/device/VulkanContext.h"
#include "rendering/vulkan/presentation/VulkanFrameLoop.h"
#include "rendering/vulkan/world/VulkanWorld.h"
#include "splat/core/Result.h"
#include "splat/math/Mat4.h"
#include "splat/math/Vec3.h"

namespace splatkit {

// The Gaussian splat pipeline: one instanced draw, four vertices per splat, blended
// back to front with no depth test. Owns the descriptor set layout, the pipeline and
// one camera uniform per frame in flight.
class VulkanSplatPipeline {
 public:
  static splat::Result<std::unique_ptr<VulkanSplatPipeline>> create(const VulkanContext& ctx,
                                                                    VkRenderPass renderPass,
                                                                    bool swapchainIsSrgb);
  ~VulkanSplatPipeline();

  VulkanSplatPipeline(const VulkanSplatPipeline&) = delete;
  VulkanSplatPipeline& operator=(const VulkanSplatPipeline&) = delete;

  // Points the descriptor set of every frame slot at this world's buffers.
  void bindWorld(const VulkanWorld& world);

  // After the frame-slot fence, before compute/draw. Borrowed uniform stays valid until
  // this pipeline is destroyed; order must cover capacity uint32s and remain alive.
  VkBuffer updateCamera(uint32_t frameSlot, const splat::Mat4& view, const splat::Mat4& proj,
                        const splat::Vec3& cameraPosition, VkExtent2D extent);
  void bindOrder(uint32_t frameSlot, VkBuffer order, uint32_t capacity);
  void drawIndirect(VkCommandBuffer cmd, uint32_t frameSlot, const VulkanWorld& world, int shDegree,
                    VkBuffer arguments);

  // Draws the first `count` entries of the order buffer with the spherical harmonics
  // bands up to `shDegree`, which must not exceed what the world was uploaded with.
  void draw(VkCommandBuffer cmd, uint32_t frameSlot, const VulkanWorld& world, uint32_t count,
            int shDegree, const splat::Mat4& view, const splat::Mat4& proj,
            const splat::Vec3& cameraPosition, VkExtent2D extent);

 private:
  explicit VulkanSplatPipeline(const VulkanContext& ctx) : ctx_(ctx) {}
  bool createDescriptors();
  bool createPipelines(VkRenderPass renderPass);
  // Binds the pipeline for `shDegree`, limited to what the world holds, and the frame's set.
  void bindDraw(VkCommandBuffer cmd, uint32_t frameSlot, const VulkanWorld& world,
                int shDegree) const;

  const VulkanContext& ctx_;
  bool outputLinear_ = true;
  VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
  VkDescriptorPool pool_ = VK_NULL_HANDLE;
  std::array<VkDescriptorSet, VulkanFrameLoop::kFramesInFlight> sets_{};
  std::array<std::unique_ptr<VulkanBuffer>, VulkanFrameLoop::kFramesInFlight> uniforms_{};
  VkPipelineLayout layout_ = VK_NULL_HANDLE;
  // One pipeline per SH degree, specialised so degree 0 worlds pay nothing for SH.
  std::array<VkPipeline, VulkanWorld::kMaxShDegree + 1> pipelines_{};
};

}  // namespace splatkit
