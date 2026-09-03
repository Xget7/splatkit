#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include "rendering/vulkan/VulkanContext.h"

namespace splatkit {

// A VkBuffer with its VMA allocation. Two flavours:
// - deviceLocal: lives in GPU memory, filled through a staging copy. Splat data.
// - hostVisible: mapped for CPU writes, read by the GPU. Uniforms and sort output.
class GpuBuffer {
 public:
  static std::unique_ptr<GpuBuffer> deviceLocal(const VulkanContext& ctx, VkDeviceSize size,
                                                VkBufferUsageFlags usage);
  static std::unique_ptr<GpuBuffer> hostVisible(const VulkanContext& ctx, VkDeviceSize size,
                                                VkBufferUsageFlags usage);
  ~GpuBuffer();

  GpuBuffer(const GpuBuffer&) = delete;
  GpuBuffer& operator=(const GpuBuffer&) = delete;

  VkBuffer handle() const { return buffer_; }
  VkDeviceSize size() const { return size_; }
  // Only for hostVisible buffers.
  void* mapped() const { return mapped_; }
  // Required after every CPU write to a hostVisible buffer: VMA only prefers coherent
  // memory, it does not guarantee it. A no-op when the memory is coherent.
  void flush(VkDeviceSize offset, VkDeviceSize size) const;

  // Blocking upload through a staging buffer and a one-shot command buffer.
  // Fine for a world load; not for per-frame data.
  bool upload(const void* data, VkDeviceSize size);

 private:
  explicit GpuBuffer(const VulkanContext& ctx) : ctx_(ctx) {}

  const VulkanContext& ctx_;
  VkBuffer buffer_ = VK_NULL_HANDLE;
  VmaAllocation allocation_ = VK_NULL_HANDLE;
  VkDeviceSize size_ = 0;
  void* mapped_ = nullptr;
};

}  // namespace splatkit
