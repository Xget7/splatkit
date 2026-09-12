#include "rendering/vulkan/VulkanFrameCompute.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "rendering/vulkan/RadixSort.h"
#include "splatkit/Log.h"
#include "splatkit/rendering/GpuLayout.h"

namespace splatkit {
namespace {
void dependency(VkCommandBuffer cmd, VkPipelineStageFlags source, VkAccessFlags sourceAccess,
                VkPipelineStageFlags target, VkAccessFlags targetAccess) {
  VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  barrier.srcAccessMask = sourceAccess;
  barrier.dstAccessMask = targetAccess;
  vkCmdPipelineBarrier(cmd, source, target, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}
constexpr uint32_t kMaxVisible = 3000000;
constexpr VkDeviceSize kReadbackBytes = 32;
}  // namespace

VulkanFrameCompute::VulkanFrameCompute(const VulkanContext& ctx) : ctx_(ctx) {}

splat::Result<std::unique_ptr<VulkanFrameCompute>> VulkanFrameCompute::create(
    const VulkanContext& ctx, uint32_t sourceCount, const splat::LodTree* tree, uint32_t budget) {
  std::unique_ptr<VulkanFrameCompute> pass(new VulkanFrameCompute(ctx));
  if (!pass->initialize(sourceCount, tree, budget))
    return splat::Error{splat::ErrorCode::gpuUnavailable, "GPU frame allocation/capability limits"};
  return pass;
}

bool VulkanFrameCompute::initialize(uint32_t sourceCount, const splat::LodTree* tree,
                                    uint32_t budget) {
  sourceCount_ = sourceCount;
  auto visibility = VisibilityPass::create(ctx_);
  if (!visibility) return false;
  visibility_ = std::move(visibility.value());
  if (VkDeviceSize{std::max(1u, sourceCount)} * sizeof(GpuSplat) >
      visibility_->capabilities().maxStorageBufferRange)
    return false;
  capacity_ = std::max(1u, std::min(sourceCount, kMaxVisible));
  if (tree) {
    if (tree->nodeCount() != sourceCount) return false;
    auto lod = LodSelection::create(ctx_);
    if (!lod || !lod.value()->upload(*tree, budget)) return false;
    lod_ = std::move(lod.value());
    capacity_ = lod_->capacity();
  }
  auto radix = RadixSort::create(ctx_);
  if (!radix || !radix.value()->reserve(capacity_) || !visibility_->reserve(capacity_))
    return false;
  radix_ = std::move(radix.value());
  if (const char* bits = std::getenv("SPLATKIT_VULKAN_SORT_BITS"))
    if (std::strcmp(bits, "16") == 0) keyBits_ = 16;

  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(ctx_.physicalDevice(), &properties);
  uint32_t familyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(ctx_.physicalDevice(), &familyCount, nullptr);
  std::vector<VkQueueFamilyProperties> families(familyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(ctx_.physicalDevice(), &familyCount, families.data());
  timestampBits_ = families[ctx_.queueFamily()].timestampValidBits;
  timestampPeriod_ = properties.limits.timestampPeriod;
  if (!properties.limits.timestampComputeAndGraphics) timestampBits_ = 0;
  for (uint32_t slot = 0; slot < kSlots; ++slot) {
    ranges_[slot] = GpuBuffer::hostVisible(ctx_, kMaxRanges * sizeof(RangeRecord),
                                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    readback_[slot] =
        GpuBuffer::hostVisible(ctx_, kReadbackBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    if (!ranges_[slot] || !readback_[slot]) return false;
    if (timestampBits_) {
      VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
      info.queryType = VK_QUERY_TYPE_TIMESTAMP;
      info.queryCount = 4;
      if (vkCreateQueryPool(ctx_.device(), &info, nullptr, &queries_[slot]) != VK_SUCCESS)
        return false;
    }
  }
  LOGI("Vulkan GPU frame: resident %u, visible capacity %u, LOD %d, radix bits %u", sourceCount_,
       capacity_, lod_ ? 1 : 0, keyBits_);
  return true;
}

VulkanFrameCompute::~VulkanFrameCompute() {
  for (VkQueryPool pool : queries_)
    if (pool) vkDestroyQueryPool(ctx_.device(), pool, nullptr);
}

bool VulkanFrameCompute::prepareRanges(uint32_t slot, const SplatRenderer::Frame& frame,
                                       VisibilityPass::Input& input) {
  if (frame.rangeCount > kMaxRanges || (frame.rangeCount && !frame.ranges)) return false;
  rangeRecords_.clear();
  for (uint32_t i = 0; i < frame.rangeCount; ++i) {
    const auto& range = frame.ranges[i];
    if (range.offset > sourceCount_ || range.count > sourceCount_ - range.offset) return false;
    if (range.count) rangeRecords_.push_back({range.offset, range.count, 0, 0});
  }
  std::sort(rangeRecords_.begin(), rangeRecords_.end(),
            [](const RangeRecord& a, const RangeRecord& b) { return a.offset < b.offset; });
  uint32_t end = 0, count = 0;
  for (auto& range : rangeRecords_) {
    if (range.offset < end) return false;
    end = range.offset + range.count;
    count += range.count;  // Nonoverlap and source bounds prove this cannot overflow.
    range.prefixEnd = count;
  }
  if (!rangeRecords_.empty()) {
    const size_t bytes = rangeRecords_.size() * sizeof(RangeRecord);
    std::memcpy(ranges_[slot]->mapped(), rangeRecords_.data(), bytes);
    ranges_[slot]->flush(0, bytes);
  }
  input.mode = VisibilityPass::CandidateMode::ranges;
  input.candidates = ranges_[slot]->handle();
  input.candidatesBytes = ranges_[slot]->size();
  input.candidateCapacity = count;
  input.rangeCount = static_cast<uint32_t>(rangeRecords_.size());
  return true;
}

void VulkanFrameCompute::collect(uint32_t slot) {
  if (!pending_[slot]) return;
  pending_[slot] = false;
  readback_[slot]->invalidate(0, kReadbackBytes);
  const auto* words = static_cast<const uint32_t*>(readback_[slot]->mapped());
  stats_.drawn = words[0];
  stats_.selected = words[1];
  stats_.limited = words[2];
  stats_.evaluated = words[3];
  stats_.status = words[4] | (words[5] << 16);
  if (stats_.status) LOGE("Vulkan GPU frame rejected: diagnostic bits 0x%x", stats_.status);
  if (!queries_[slot]) return;
  uint64_t ticks[4]{};
  if (vkGetQueryPoolResults(ctx_.device(), queries_[slot], 0, 4, sizeof(ticks), ticks,
                            sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
    return;
  const uint64_t mask = timestampBits_ >= 64 ? std::numeric_limits<uint64_t>::max()
                                             : (uint64_t{1} << timestampBits_) - 1;
  const double millis = static_cast<double>(timestampPeriod_) * 1e-6;
  stats_.selectMillis = lod_ ? ((ticks[1] - ticks[0]) & mask) * millis : 0;
  stats_.sortMillis = ((ticks[3] - ticks[2]) & mask) * millis;
}

void VulkanFrameCompute::copyDiagnostics(VkCommandBuffer cmd, uint32_t slot,
                                         const VisibilityPass::Output& visible,
                                         uint32_t candidates) {
  auto target = readback_[slot]->handle();
  uint32_t words[8]{0, candidates, 0, 0, 0, 0, 0, 0};
  vkCmdUpdateBuffer(cmd, target, 0, sizeof(words), words);
  dependency(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
             VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT,
             VK_PIPELINE_STAGE_TRANSFER_BIT,
             VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
  const auto copy = [&](VkBuffer source, VkDeviceSize from, VkDeviceSize to, VkDeviceSize bytes) {
    const VkBufferCopy region{from, to, bytes};
    vkCmdCopyBuffer(cmd, source, target, 1, &region);
  };
  const auto sorted = radix_->output(slot);
  copy(sorted.count, 0, 0, 4);
  copy(visible.status, 0, 16, 4);
  copy(sorted.status, 0, 20, 4);
  if (lod_) {
    copy(lod_->output().state, LodSelection::kCountOffset, 4, 4);
    copy(lod_->output().state, LodSelection::kLimitedOffset, 8, 8);
  }
  dependency(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
             VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
  pending_[slot] = true;
}

std::optional<VulkanFrameCompute::Draw> VulkanFrameCompute::encode(
    VkCommandBuffer cmd, uint32_t slot, VkBuffer camera, const GpuBuffer& splats,
    const SplatRenderer::Frame& frame) {
  if (!cmd || slot >= kSlots || !camera || frame.orderSource != SplatRenderer::OrderSource::gpu)
    return std::nullopt;
  collect(slot);
  VisibilityPass::Input input;
  input.camera = camera;
  input.splats = splats.handle();
  input.splatsBytes = splats.size();
  input.sourceCount = sourceCount_;
  input.keyBits = keyBits_ == 16 ? VisibilityPass::KeyBits::low16 : VisibilityPass::KeyBits::full32;
  input.keyOrder = VisibilityPass::KeyOrder::descending;  // Hardware uses back-to-front over.
  if (!lod_ && !prepareRanges(slot, frame, input)) return std::nullopt;
  dependency(cmd, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
             VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
             VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT);
  if (queries_[slot]) vkCmdResetQueryPool(cmd, queries_[slot], 0, 4);
  const auto timestamp = [&](uint32_t index) {
    if (queries_[slot])
      vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queries_[slot], index);
  };
  timestamp(0);
  if (lod_) {
    if (!lod_->encode(cmd, slot, {camera, 0})) return std::nullopt;
    const auto selected = lod_->output();
    input.mode = VisibilityPass::CandidateMode::indices;
    input.candidates = selected.indices;
    input.candidatesBytes = VkDeviceSize{selected.capacity} * sizeof(uint32_t);
    input.candidateCount = selected.state;
    input.candidateCountBytes = LodSelection::kDiagnosticBytes;
    input.candidateCapacity = selected.capacity;
  }
  timestamp(1);
  if (!visibility_->encode(cmd, slot, input)) return std::nullopt;
  const auto visible = visibility_->output(slot);
  timestamp(2);
  RadixSort::Input sort;
  sort.keys = visible.depthKeys;
  sort.values = visible.indices;
  sort.count = visible.count;
  sort.keysBytes = sort.valuesBytes = VkDeviceSize{capacity_} * sizeof(uint32_t);
  sort.countBytes = sizeof(uint32_t);
  sort.keyBits = keyBits_ == 16 ? RadixSort::KeyBits::low16 : RadixSort::KeyBits::full32;
  if (!radix_->encode(cmd, slot, sort)) return std::nullopt;
  timestamp(3);
  // The sorter's checked GPU count is authoritative even if visibility succeeded.
  dependency(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
             VK_PIPELINE_STAGE_TRANSFER_BIT,
             VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
  const VkBufferCopy countToDraw{0, offsetof(VkDrawIndirectCommand, instanceCount),
                                 sizeof(uint32_t)};
  vkCmdCopyBuffer(cmd, radix_->output(slot).count, visible.indirect, 1, &countToDraw);
  dependency(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
             VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT);
  copyDiagnostics(cmd, slot, visible, input.candidateCapacity);
  return Draw{radix_->output(slot).values, visible.indirect, capacity_};
}

}  // namespace splatkit
