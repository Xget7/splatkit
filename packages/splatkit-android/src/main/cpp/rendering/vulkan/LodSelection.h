#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "rendering/vulkan/GpuBuffer.h"
#include "splat/lod/LodTree.h"

namespace splatkit {

// Full hierarchy residency; interior-only GPU traversal and cooperative leaf packets.
// SSE matches MetalLOD's covariance/appearance metadata heuristic, not an image-error bound.
// Render thread only. Context outlives this pass. Upload/destroy require all consumers idle.
// One scratch/output domain: encode AND consumers must be ordered on the same context queue.
// The next encode overwrites output; copy diagnostics before it. No implicit submit/wait.
class LodSelection {
 public:
  struct Quality {
    float pixelLimit = 1.0f;  // zero requests exact leaves if capacity permits
    float colorWeight = 4.0f;
    bool frustumCull = true;
  };
  struct Input {
    VkBuffer camera = VK_NULL_HANDLE;  // CameraUniform, 176 bytes, caller-owned/flush before encode
    VkDeviceSize cameraOffset = 0;     // minUniformBufferOffsetAlignment; buffer must cover range
  };
  struct Output {
    VkBuffer indices =
        VK_NULL_HANDLE;               // original LodTree node indices, uint32, count valid entries
    VkBuffer state = VK_NULL_HANDLE;  // count/diagnostics at constants below, TRANSFER_SRC capable
    uint32_t capacity = 0;
  };
  static constexpr uint32_t kSlots = 2;
  static constexpr VkDeviceSize kCountOffset = 0;
  static constexpr VkDeviceSize kLimitedOffset = 16;
  static constexpr VkDeviceSize kEvaluatedOffset = 20;
  static constexpr VkDeviceSize kDiagnosticBytes = 24;

  static splat::Result<std::unique_ptr<LodSelection>> create(const VulkanContext& ctx);
  ~LodSelection();
  LodSelection(const LodSelection&) = delete;
  LodSelection& operator=(const LodSelection&) = delete;

  // Transactional, bounded allocation/upload; failure preserves old hierarchy and descriptors.
  // Budget must be 1..2,200,000; effective capacity is min(budget, original leaf count).
  // Source metadata, scratch and output must each fit maxStorageBufferRange.
  // Metadata may be built once at upload for v1 trees, never on the CPU per frame.
  bool upload(const splat::LodTree& tree, uint32_t budget, Quality quality);
  bool upload(const splat::LodTree& tree, uint32_t budget) {
    return upload(tree, budget, Quality{});
  }
  // Fence before reusing this descriptor slot; camera remains valid through completion.
  // Publishes indices/count to compute/indirect/transfer consumers with Vulkan 1.1 barriers.
  bool encode(VkCommandBuffer cmd, uint32_t slot, const Input& input) const;
  Output output() const;
  uint32_t capacity() const { return config_.capacity; }

 private:
  explicit LodSelection(const VulkanContext& ctx) : ctx_(ctx) {}
  bool initialize();
  struct Config {
    uint32_t phase = 0, capacity = 0, costs = 0, offsets = 0;
    uint32_t costGroups = 0, groups = 0, blocks = 0, frontier0 = 0;
    uint32_t frontier1 = 0, packets = 0;
    float pixelLimit = 1, colorWeight = 4;
    uint32_t cull = 1;
  } config_;
  const VulkanContext& ctx_;
  VkPhysicalDeviceLimits limits_{};
  uint32_t rounds_ = 0;
  VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
  VkDescriptorPool pool_ = VK_NULL_HANDLE;
  std::array<VkDescriptorSet, kSlots> sets_{};
  VkPipelineLayout layout_ = VK_NULL_HANDLE;
  VkPipeline pipeline_ = VK_NULL_HANDLE;
  std::unique_ptr<GpuBuffer> clusters_, leaves_, scratch_, indices_;
};

}  // namespace splatkit
