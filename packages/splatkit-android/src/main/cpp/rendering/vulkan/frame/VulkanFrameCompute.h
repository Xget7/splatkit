#pragma once

#include <array>
#include <memory>
#include <vector>

#include "rendering/vulkan/cull/VulkanVisibilityPass.h"
#include "rendering/vulkan/device/VulkanBuffer.h"
#include "rendering/vulkan/layout/VulkanFrameSlots.h"
#include "rendering/vulkan/layout/VulkanSortKeys.h"
#include "rendering/vulkan/selection/VulkanLodSelection.h"
#include "splatkit/rendering/SplatRenderer.h"

namespace splatkit {

class VulkanRadixSort;

// Resident-world GPU ordering. Owns LOD, culling, sorting and delayed diagnostics.
// Render thread only. Context/inputs outlive submissions. Create/destroy while idle;
// encode only after the caller's slot fence, with consumers on the same queue.
// Counts never return to CPU to decide dispatch/draw. Diagnostics lag by frame slots: a
// slot's are read when it is encoded again, or once its submission is known finished.
class VulkanFrameCompute {
 public:
  // Most splats one frame can keep, which bounds the visibility output and the sort.
  static constexpr uint32_t kMaxVisible = VulkanVisibilityPass::kMaxCapacity;
  // True when the device can run the GPU visibility and sort path.
  static bool supported(const VulkanContext& ctx);
  static splat::Result<std::unique_ptr<VulkanFrameCompute>> create(
      const VulkanContext& ctx, uint32_t sourceCount, const splat::LodTree* tree = nullptr,
      uint32_t budget = VulkanLodSelection::kMaxBudget);
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
    double sortMillis = 0, selectMillis = 0, cullMillis = 0;
  };
  // Nullopt means encoding failed; caller must still submit/end its acquired frame.
  std::optional<Draw> encode(VkCommandBuffer cmd, uint32_t slot, VkBuffer camera,
                             const VulkanBuffer& splats, const SplatRenderer::Frame& frame);
  const Stats& stats() const { return stats_; }
  // The queue submission that carried the slot's last encode.
  void submitted(uint32_t slot, uint64_t submission);
  // Reads the diagnostics of every submitted frame up to `completedSubmission`, so a still
  // scene that encodes nothing new reports its last frame. Never moves back to an older one.
  void collectCompleted(uint64_t completedSubmission);
  bool hasLod() const { return lod_ != nullptr; }

  // Per-instance renderer policy. Render thread, while idle. Only sortDepth and
  // subpixelThreshold are honoured; the caller resolves the rest.
  bool applyRenderPolicy(const RenderPolicy& policy, std::string* reason);
  uint32_t sortKeyBits() const { return keyBitCount(keyBits()); }
  float minPixelRadius() const { return visibility_ ? visibility_->minPixelRadius() : 0.0f; }
  const VulkanVisibilityCapabilities& visibilityCapabilities() const {
    return visibility_->capabilities();
  }

 private:
  explicit VulkanFrameCompute(const VulkanContext& ctx);
  KeyBits keyBits() const {
    return sortKeyBits_ == SortKeyBits::low16 ? KeyBits::low16 : KeyBits::full32;
  }
  bool initialize(uint32_t sourceCount, const splat::LodTree* tree, uint32_t budget);
  bool prepareRanges(uint32_t slot, const SplatRenderer::Frame& frame,
                     VulkanVisibilityPass::Input& input);
  void collect(uint32_t slot);
  void copyDiagnostics(VkCommandBuffer cmd, uint32_t slot,
                       const VulkanVisibilityPass::Output& visible, uint32_t candidates);

  struct RangeRecord {
    uint32_t offset, count, prefixEnd, pad;
  };
  static constexpr uint32_t kMaxRanges = 65536;
  const VulkanContext& ctx_;
  uint32_t sourceCount_ = 0, capacity_ = 0;
  SortKeyBits sortKeyBits_ = SortKeyBits::full32;
  uint32_t timestampBits_ = 0;
  float timestampPeriod_ = 0;
  std::unique_ptr<VulkanLodSelection> lod_;
  std::unique_ptr<VulkanVisibilityPass> visibility_;
  std::unique_ptr<VulkanRadixSort> radix_;
  std::array<std::unique_ptr<VulkanBuffer>, kFramesInFlight> ranges_, readback_;
  std::array<VkQueryPool, kFramesInFlight> queries_{};
  std::array<bool, kFramesInFlight> pending_{};
  std::array<uint64_t, kFramesInFlight>
      submission_{};  // of the pending encode; 0 when not submitted
  uint64_t collectedSubmission_ = 0;
  std::vector<RangeRecord> rangeRecords_;
  Stats stats_;
};

}  // namespace splatkit
