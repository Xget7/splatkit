#pragma once

#include <array>
#include <memory>
#include <vector>

#include "rendering/vulkan/GpuBuffer.h"
#include "rendering/vulkan/LodSelection.h"
#include "rendering/vulkan/VisibilityPass.h"
#include "splatkit/rendering/SplatRenderer.h"

namespace splatkit {

class RadixSort;

// Resident-world GPU ordering. Owns LOD, culling, sorting and delayed diagnostics.
// Render thread only. Context/inputs outlive submissions. Create/destroy while idle;
// encode only after the caller's slot fence, with consumers on the same queue.
// Counts never return to CPU to decide dispatch/draw. Diagnostics lag by frame slots.
class VulkanFrameCompute {
 public:
  static splat::Result<std::unique_ptr<VulkanFrameCompute>> create(
      const VulkanContext& ctx, uint32_t sourceCount, const splat::LodTree* tree = nullptr,
      uint32_t budget = 2200000);
  ~VulkanFrameCompute();
  VulkanFrameCompute(const VulkanFrameCompute&) = delete;
  VulkanFrameCompute& operator=(const VulkanFrameCompute&) = delete;

  struct Draw {
    VkBuffer order = VK_NULL_HANDLE;
    VkBuffer arguments = VK_NULL_HANDLE;
    uint32_t capacity = 0;
  };
  struct Stats {
    uint32_t drawn = 0, selected = 0, limited = 0, evaluated = 0, status = 0;
    double sortMillis = 0, selectMillis = 0;
  };
  // Nullopt means encoding failed; caller must still submit/end its acquired frame.
  std::optional<Draw> encode(VkCommandBuffer cmd, uint32_t slot, VkBuffer camera,
                             const GpuBuffer& splats, const SplatRenderer::Frame& frame);
  const Stats& stats() const { return stats_; }
  bool hasLod() const { return lod_ != nullptr; }

 private:
  explicit VulkanFrameCompute(const VulkanContext& ctx);
  bool initialize(uint32_t sourceCount, const splat::LodTree* tree, uint32_t budget);
  bool prepareRanges(uint32_t slot, const SplatRenderer::Frame& frame,
                     VisibilityPass::Input& input);
  void collect(uint32_t slot);
  void copyDiagnostics(VkCommandBuffer cmd, uint32_t slot, const VisibilityPass::Output& visible,
                       uint32_t candidates);

  struct RangeRecord {
    uint32_t offset, count, prefixEnd, pad;
  };
  static constexpr uint32_t kSlots = 2;
  static constexpr uint32_t kMaxRanges = 65536;
  const VulkanContext& ctx_;
  uint32_t sourceCount_ = 0, capacity_ = 0;
  uint32_t keyBits_ = 32;
  uint32_t timestampBits_ = 0;
  float timestampPeriod_ = 0;
  std::unique_ptr<LodSelection> lod_;
  std::unique_ptr<VisibilityPass> visibility_;
  std::unique_ptr<RadixSort> radix_;
  std::array<std::unique_ptr<GpuBuffer>, kSlots> ranges_, readback_;
  std::array<VkQueryPool, kSlots> queries_{};
  std::array<bool, kSlots> pending_{};
  std::vector<RangeRecord> rangeRecords_;
  Stats stats_;
};

}  // namespace splatkit
