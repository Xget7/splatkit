#pragma once

#include <cstddef>
#include <cstdint>

#include <vulkan/vulkan.h>

namespace splatkit {

// A shader module that lives for one pipeline build and is destroyed on every exit from it.
// `size` is in bytes, as the generated shaders::*_size constants are. False on failure.
class ShaderModule {
 public:
  ShaderModule(VkDevice device, const uint32_t* code, size_t size) : device_(device) {
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = size;
    info.pCode = code;
    if (vkCreateShaderModule(device, &info, nullptr, &module_) != VK_SUCCESS)
      module_ = VK_NULL_HANDLE;
  }
  ~ShaderModule() {
    if (module_ != VK_NULL_HANDLE) vkDestroyShaderModule(device_, module_, nullptr);
  }
  ShaderModule(const ShaderModule&) = delete;
  ShaderModule& operator=(const ShaderModule&) = delete;

  explicit operator bool() const { return module_ != VK_NULL_HANDLE; }
  VkShaderModule get() const { return module_; }

 private:
  VkDevice device_;
  VkShaderModule module_ = VK_NULL_HANDLE;
};

}  // namespace splatkit
