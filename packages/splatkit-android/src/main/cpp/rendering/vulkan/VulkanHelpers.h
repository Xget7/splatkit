#pragma once

#include <cstddef>
#include <cstdint>

#include <vulkan/vulkan.h>

namespace splatkit {

// One global memory barrier; the passes order whole buffers, never single ranges.
inline void memoryBarrier(VkCommandBuffer cmd, VkPipelineStageFlags srcStage,
                          VkAccessFlags srcAccess, VkPipelineStageFlags dstStage,
                          VkAccessFlags dstAccess) {
  VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  barrier.srcAccessMask = srcAccess;
  barrier.dstAccessMask = dstAccess;
  vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

// `size` is in bytes, as the generated shaders::*_size constants are. Null on failure.
inline VkShaderModule createShaderModule(VkDevice device, const uint32_t* code, size_t size) {
  VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  info.codeSize = size;
  info.pCode = code;
  VkShaderModule module = VK_NULL_HANDLE;
  if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) return VK_NULL_HANDLE;
  return module;
}

}  // namespace splatkit
