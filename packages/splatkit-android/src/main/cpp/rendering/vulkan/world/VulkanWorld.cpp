#include "rendering/vulkan/world/VulkanWorld.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

#include "splatkit/rendering/GpuLayout.h"

namespace splatkit {
namespace {

// How many bytes of splats one packing and upload window covers.
constexpr size_t kUploadWindowBytes = 2 * 1024 * 1024;

}  // namespace

std::unique_ptr<VulkanWorld> VulkanWorld::upload(const VulkanContext& ctx,
                                                 const splat::SplatCloud& cloud, int maxShDegree,
                                                 bool cpuOrder) {
  const size_t n = cloud.count();
  if (n > std::numeric_limits<uint32_t>::max() || cloud.positions.size() != n * 3 ||
      cloud.covariances.size() != n * 6 || cloud.colors.size() != n * 3 || cloud.alphas.size() != n)
    return nullptr;
  const int requestedDegree = std::clamp(std::min(cloud.shDegree, maxShDegree), 0, kMaxShDegree);
  const int shDegree = carriesSh(cloud, requestedDegree) ? requestedDegree : 0;
  const size_t stride = shDegree ? shStride(shDegree) : 0;
  const VkDeviceSize sourceBytes = std::max(size_t{1}, n) * sizeof(GpuSplat);
  const VkDeviceSize orderBytes = (cpuOrder ? std::max(size_t{1}, n) : 1) * sizeof(uint32_t);
  const VkDeviceSize shBytes = std::max(size_t{1}, n * stride) * sizeof(uint32_t);
  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(ctx.physicalDevice(), &properties);
  if (std::max({sourceBytes, orderBytes, shBytes}) > properties.limits.maxStorageBufferRange)
    return nullptr;

  std::unique_ptr<VulkanWorld> world(new VulkanWorld());
  world->count = static_cast<uint32_t>(n);
  world->shDegree = shDegree;
  world->splats = VulkanBuffer::deviceLocal(ctx, sourceBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  world->order = VulkanBuffer::deviceLocal(ctx, orderBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  world->sh = VulkanBuffer::deviceLocal(ctx, shBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  if (!world->splats || !world->order || !world->sh) return nullptr;
  for (auto& staging : world->orderStaging) {
    if (!cpuOrder) break;
    staging = VulkanBuffer::hostVisible(ctx, orderBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    if (!staging) return nullptr;
  }
  // Bounded packing/upload windows: do not retain full packed + mapped copies of a 10M cloud.
  const size_t chunk = kUploadWindowBytes / std::max(sizeof(GpuSplat), stride * sizeof(uint32_t));
  std::vector<GpuSplat> packed(std::min(n, chunk));
  std::vector<uint32_t> sh(std::min(n, chunk) * stride);
  std::vector<uint32_t> order(cpuOrder ? std::min(n, chunk) : 0);
  for (size_t offset = 0; offset < n; offset += chunk) {
    const size_t count = std::min(chunk, n - offset);
    packSplatRange(cloud, offset, count, packed.data());
    if (!world->splats->upload(offset * sizeof(GpuSplat), packed.data(), count * sizeof(GpuSplat)))
      return nullptr;
    if (stride) {
      packShRange(cloud, shDegree, offset, count, sh.data());
      if (!world->sh->upload(offset * stride * sizeof(uint32_t), sh.data(),
                             count * stride * sizeof(uint32_t)))
        return nullptr;
    }
    if (cpuOrder) {
      for (size_t i = 0; i < count; ++i) order[i] = static_cast<uint32_t>(offset + i);
      if (!world->order->upload(offset * sizeof(uint32_t), order.data(), count * sizeof(uint32_t)))
        return nullptr;
    }
  }
  const uint32_t zero = 0;
  if ((!cpuOrder || !n) && !world->order->upload(&zero, sizeof(zero))) return nullptr;
  if ((!stride || !n) && !world->sh->upload(&zero, sizeof(zero))) return nullptr;
  return world;
}

std::unique_ptr<VulkanWorld> VulkanWorld::createSlab(const VulkanContext& ctx, uint32_t capacity,
                                                     int shDegree, bool cpuOrder) {
  if (capacity == 0) return nullptr;
  std::unique_ptr<VulkanWorld> world(new VulkanWorld());
  world->count = capacity;
  world->shDegree = std::clamp(shDegree, 0, kMaxShDegree);
  const VkDeviceSize shBytes =
      world->shDegree > 0 ? VkDeviceSize{capacity} * shStride(world->shDegree) * sizeof(uint32_t)
                          : sizeof(uint32_t);
  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(ctx.physicalDevice(), &properties);
  if (std::max(shBytes, VkDeviceSize{capacity} * sizeof(GpuSplat)) >
      properties.limits.maxStorageBufferRange)
    return nullptr;
  world->splats = VulkanBuffer::deviceLocal(ctx, VkDeviceSize{capacity} * sizeof(GpuSplat),
                                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  world->order =
      VulkanBuffer::deviceLocal(ctx, VkDeviceSize{cpuOrder ? capacity : 1u} * sizeof(uint32_t),
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  world->sh = VulkanBuffer::deviceLocal(ctx, shBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  if (!world->splats || !world->order || !world->sh) return nullptr;
  for (auto& staging : world->orderStaging) {
    if (!cpuOrder) break;
    staging = VulkanBuffer::hostVisible(ctx, VkDeviceSize{capacity} * sizeof(uint32_t),
                                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    if (!staging) return nullptr;
  }
  return world;
}

bool VulkanWorld::uploadTile(uint32_t offset, const splat::SplatCloud& cloud) const {
  const size_t n = cloud.count();
  if (n == 0) return true;
  if (offset > count || n > count - offset) return false;
  const std::vector<GpuSplat> packed = packSplats(cloud);
  if (!splats->upload(VkDeviceSize{offset} * sizeof(GpuSplat), packed.data(),
                      packed.size() * sizeof(GpuSplat))) {
    return false;
  }
  if (shDegree == 0) return true;
  const size_t stride = shStride(shDegree);
  const std::vector<uint32_t> packedSh =
      carriesSh(cloud, shDegree) ? packSh(cloud, shDegree) : std::vector<uint32_t>(n * stride, 0);
  return sh->upload(VkDeviceSize{offset} * stride * sizeof(uint32_t), packedSh.data(),
                    packedSh.size() * sizeof(uint32_t));
}

void VulkanWorld::recordOrderUpload(VkCommandBuffer cmd, uint32_t frameSlot,
                                    const uint32_t* drawOrder, uint32_t drawCount) const {
  const VkDeviceSize bytes = std::min(drawCount, count) * sizeof(uint32_t);
  if (bytes == 0 || !orderStaging[frameSlot]) return;
  const VulkanBuffer& staging = *orderStaging[frameSlot];
  std::memcpy(staging.mapped(), drawOrder, static_cast<size_t>(bytes));
  staging.flush(0, bytes);

  // The previous frame may still be reading the order buffer in its vertex shader.
  // Queue order alone does not prevent the copy from overlapping it (write after read),
  // so this barrier makes the transfer wait for those reads.
  VkBufferMemoryBarrier beforeCopy{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  beforeCopy.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
  beforeCopy.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  beforeCopy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  beforeCopy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  beforeCopy.buffer = order->handle();
  beforeCopy.offset = 0;
  beforeCopy.size = bytes;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                       0, nullptr, 1, &beforeCopy, 0, nullptr);

  const VkBufferCopy region{0, 0, bytes};
  vkCmdCopyBuffer(cmd, staging.handle(), order->handle(), 1, &region);

  // And this frame's vertex shader must see the copy complete (read after write).
  VkBufferMemoryBarrier afterCopy = beforeCopy;
  afterCopy.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  afterCopy.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0,
                       0, nullptr, 1, &afterCopy, 0, nullptr);
}

}  // namespace splatkit
