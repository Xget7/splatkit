#pragma once

#include <memory>

#include <vulkan/vulkan.h>

#include "rendering/vulkan/VulkanContext.h"
#include "splat/core/Result.h"

namespace splatkit {

// Draws one triangle with no buffers or descriptors. The renderer shows it while no world is
// loaded, as the smallest possible "is Vulkan alive" signal.
class DebugTrianglePipeline {
 public:
  static splat::Result<std::unique_ptr<DebugTrianglePipeline>> create(const VulkanContext& ctx,
                                                                      VkRenderPass renderPass);
  ~DebugTrianglePipeline();

  DebugTrianglePipeline(const DebugTrianglePipeline&) = delete;
  DebugTrianglePipeline& operator=(const DebugTrianglePipeline&) = delete;

  void draw(VkCommandBuffer cmd) const;

 private:
  explicit DebugTrianglePipeline(const VulkanContext& ctx) : ctx_(ctx) {}

  const VulkanContext& ctx_;
  VkPipelineLayout layout_ = VK_NULL_HANDLE;
  VkPipeline pipeline_ = VK_NULL_HANDLE;
};

}  // namespace splatkit
