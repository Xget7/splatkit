#include "rendering/vulkan/GpuBuffer.h"

#include <cstring>

#include "Log.h"

namespace splatkit {
namespace {

bool create(const VulkanContext& ctx, VkDeviceSize size, VkBufferUsageFlags usage,
            VmaAllocationCreateFlags flags, bool map, VkBuffer& buffer,
            VmaAllocation& allocation, void*& mapped) {
  VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  info.size = size;
  info.usage = usage;
  info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo alloc{};
  alloc.usage = VMA_MEMORY_USAGE_AUTO;
  alloc.flags = flags | (map ? VMA_ALLOCATION_CREATE_MAPPED_BIT : 0);

  VmaAllocationInfo result{};
  if (vmaCreateBuffer(ctx.allocator(), &info, &alloc, &buffer, &allocation, &result) != VK_SUCCESS) {
    LOGE("vmaCreateBuffer failed for %llu bytes", static_cast<unsigned long long>(size));
    return false;
  }
  mapped = map ? result.pMappedData : nullptr;
  return true;
}

}  // namespace

std::unique_ptr<GpuBuffer> GpuBuffer::deviceLocal(const VulkanContext& ctx, VkDeviceSize size,
                                                  VkBufferUsageFlags usage) {
  std::unique_ptr<GpuBuffer> b(new GpuBuffer(ctx));
  b->size_ = size;
  create(ctx, size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 0, false, b->buffer_,
         b->allocation_, b->mapped_);
  return b->buffer_ != VK_NULL_HANDLE ? std::move(b) : nullptr;
}

std::unique_ptr<GpuBuffer> GpuBuffer::hostVisible(const VulkanContext& ctx, VkDeviceSize size,
                                                  VkBufferUsageFlags usage) {
  std::unique_ptr<GpuBuffer> b(new GpuBuffer(ctx));
  b->size_ = size;
  create(ctx, size, usage,
         VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, true, b->buffer_,
         b->allocation_, b->mapped_);
  return b->buffer_ != VK_NULL_HANDLE ? std::move(b) : nullptr;
}

GpuBuffer::~GpuBuffer() {
  if (buffer_ != VK_NULL_HANDLE) vmaDestroyBuffer(ctx_.allocator(), buffer_, allocation_);
}

void GpuBuffer::flush(VkDeviceSize offset, VkDeviceSize size) const {
  vmaFlushAllocation(ctx_.allocator(), allocation_, offset, size);
}

bool GpuBuffer::upload(const void* data, VkDeviceSize size) {
  if (size > size_) return false;
  auto staging = hostVisible(ctx_, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
  if (!staging) return false;
  std::memcpy(staging->mapped(), data, static_cast<size_t>(size));
  staging->flush(0, size);

  VkDevice device = ctx_.device();
  VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  poolInfo.queueFamilyIndex = ctx_.queueFamily();
  poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  VkCommandPool pool = VK_NULL_HANDLE;
  if (vkCreateCommandPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS) return false;

  VkCommandBufferAllocateInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  cmdInfo.commandPool = pool;
  cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmdInfo.commandBufferCount = 1;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  VkBufferCopy region{0, 0, size};
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &cmd;
  // Each step can fail under memory pressure, which is when a 2M splat upload runs.
  const bool ok = vkAllocateCommandBuffers(device, &cmdInfo, &cmd) == VK_SUCCESS &&
                  vkBeginCommandBuffer(cmd, &begin) == VK_SUCCESS &&
                  (vkCmdCopyBuffer(cmd, staging->handle(), buffer_, 1, &region), true) &&
                  vkEndCommandBuffer(cmd) == VK_SUCCESS &&
                  vkCreateFence(device, &fenceInfo, nullptr, &fence) == VK_SUCCESS &&
                  vkQueueSubmit(ctx_.queue(), 1, &submit, fence) == VK_SUCCESS &&
                  vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;

  if (fence != VK_NULL_HANDLE) vkDestroyFence(device, fence, nullptr);
  vkDestroyCommandPool(device, pool, nullptr);
  return ok;
}

}  // namespace splatkit
