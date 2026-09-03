#pragma once

#include <memory>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
// After vulkan.h on purpose: VkBootstrap.h would otherwise hide the prototypes.
#include <VkBootstrap.h>

#include "splat/core/Result.h"

namespace splatkit {

// Instance, physical device, logical device, the single graphics+present queue,
// and the memory allocator. Created once per engine; outlives every surface.
class VulkanContext {
 public:
  static splat::Result<std::unique_ptr<VulkanContext>> create();
  ~VulkanContext();

  VulkanContext(const VulkanContext&) = delete;
  VulkanContext& operator=(const VulkanContext&) = delete;

  VkInstance instance() const { return instance_.instance; }
  VkPhysicalDevice physicalDevice() const { return device_.physical_device; }
  VkDevice device() const { return device_.device; }
  VkQueue queue() const { return queue_; }
  uint32_t queueFamily() const { return queueFamily_; }
  VmaAllocator allocator() const { return allocator_; }
  const vkb::Device& vkbDevice() const { return device_; }

  // True when the queue can present to this surface. Checked every time a surface arrives.
  bool supportsPresent(VkSurfaceKHR surface) const;
  void waitIdle() const;

 private:
  VulkanContext() = default;

  vkb::Instance instance_{};
  vkb::Device device_{};
  VkQueue queue_ = VK_NULL_HANDLE;
  uint32_t queueFamily_ = 0;
  VmaAllocator allocator_ = VK_NULL_HANDLE;
};

}  // namespace splatkit
