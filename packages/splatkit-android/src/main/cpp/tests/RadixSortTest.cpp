#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <random>
#include <vector>

#include "rendering/vulkan/RadixSort.h"
#include "tests/VulkanTestContext.h"

using splatkit::GpuBuffer;
using splatkit::RadixSort;
using splatkit::test::require;
using splatkit::test::VulkanTestContext;

namespace {
std::vector<uint32_t> read(VulkanTestContext& gpu, VkBuffer source, uint32_t count) {
  if (!count) return {};
  const VkDeviceSize bytes = VkDeviceSize{count} * 4;
  auto staging = GpuBuffer::deviceLocal(
      *gpu.context, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
  require(static_cast<bool>(staging), "readback intermediate allocation");
  gpu.submit([&](VkCommandBuffer cmd) {
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         1, &barrier, 0, nullptr, 0, nullptr);
    const VkBufferCopy copy{0, 0, bytes};
    vkCmdCopyBuffer(cmd, source, staging->handle(), 1, &copy);
  });
  auto raw = gpu.readback(*staging, bytes);
  std::vector<uint32_t> result(count);
  std::memcpy(result.data(), raw.data(), raw.size());
  return result;
}
}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  try {
    VulkanTestContext gpu;
    const auto capabilities = RadixSort::queryCapabilities(*gpu.context);
    std::printf("Radix device: %s; subgroup=%u; supported=%d; %s\n",
                gpu.context->deviceDescription().c_str(), capabilities.subgroupSize,
                capabilities.supported, capabilities.reason.c_str());
    if (!capabilities.supported) {
      require(!RadixSort::create(*gpu.context), "unsupported create must fail");
      std::puts("SKIP radix GPU cases: unsupported device");
      return 77;
    }
    std::puts("radix stage=create");
    auto result = RadixSort::create(*gpu.context);
    require(static_cast<bool>(result), "create radix");
    auto sort = std::move(result.value());
    require(sort->output(0).keys == VK_NULL_HANDLE, "unreserved output");
    std::puts("radix stage=reserve-one");
    require(sort->reserve(0) && sort->capacity() == 1, "zero reserve");
    auto* const old = sort->output(0).keys;
    require(!sort->reserve(RadixSort::kMaxCapacity + 1), "reject oversized capacity");
    require(sort->capacity() == 1 && sort->output(0).keys == old, "transactional reserve failure");
    const bool large = std::getenv("SPLATKIT_RADIX_LARGE") != nullptr;
    const bool tiny = std::getenv("SPLATKIT_RADIX_TINY") != nullptr;
    const uint32_t capacity = tiny ? 257 : large ? RadixSort::kMaxCapacity : 262145;
    std::printf("radix stage=reserve capacity=%u\n", capacity);
    require(sort->reserve(capacity), "reserve test capacity");
    const VkDeviceSize bytes = VkDeviceSize{capacity} * 4;
    std::puts("radix stage=input-allocation");
    auto keys = GpuBuffer::deviceLocal(*gpu.context, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    auto values = GpuBuffer::deviceLocal(*gpu.context, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    auto count = GpuBuffer::deviceLocal(*gpu.context, 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    require(keys && values && count, "allocate input");
    RadixSort::Input input;
    input.keys = keys->handle();
    input.values = values->handle();
    input.count = count->handle();
    input.keysBytes = bytes;
    input.valuesBytes = bytes;
    require(!sort->encode(VK_NULL_HANDLE, 0, input), "reject null command buffer");
    gpu.submit([&](VkCommandBuffer cmd) {
      require(!sort->encode(cmd, RadixSort::kSlots, input), "reject invalid slot");
      auto bad = input;
      bad.keysBytes = bytes - 1;
      require(!sort->encode(cmd, 0, bad), "reject undersized input");
      bad = input;
      bad.keysOffset = 1;
      require(!sort->encode(cmd, 0, bad), "reject misaligned offset");
      bad = input;
      // Fixed uint32_t underlying type permits this value; exercise enum validation.
      // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
      bad.keyBits = static_cast<RadixSort::KeyBits>(9);
      require(!sort->encode(cmd, 0, bad), "reject key mode");
      bad = input;
      bad.keys = sort->output(1).keys;
      require(!sort->encode(cmd, 0, bad), "reject owned output alias");
    });
    std::vector<uint32_t> sizes{0, 1, 31, 32, 33, 67, 127, 128, 129, 257};
    if (!tiny) sizes.insert(sizes.end(), {2047, 2048, 2049, 4097, 262145});
    if (large && !tiny) sizes.push_back(capacity);
    std::mt19937 random(0x51a7u);
    uint32_t cases = 0;
    for (const auto bits : {RadixSort::KeyBits::full32, RadixSort::KeyBits::low16}) {
      input.keyBits = bits;
      const uint32_t mask = bits == RadixSort::KeyBits::low16 ? 65535u : UINT32_MAX;
      for (uint32_t n : sizes) {
        // Mixed full-width values, many duplicates, and all-equal input cross block boundaries.
        for (uint32_t pattern = 0; pattern < 4; ++pattern) {
          std::printf("radix case=%u bits=%u n=%u pattern=%u stage=upload\n", cases,
                      bits == RadixSort::KeyBits::low16 ? 16u : 32u, n, pattern);
          std::vector<uint32_t> inKeys(n);
          std::vector<uint32_t> inValues(n);
          std::vector<uint32_t> expected(n);
          std::iota(inValues.begin(), inValues.end(), 0);
          std::iota(expected.begin(), expected.end(), 0);
          for (uint32_t i = 0; i < n; ++i) {
            inKeys[i] = pattern == 0 ? random() : pattern == 1 ? random() % 17 : 42;
            if (pattern == 3) {
              const float depth = 1.0f + i * 0.02f;
              std::memcpy(&inKeys[i], &depth, sizeof(depth));
              inKeys[i] = ~inKeys[i];
            }
            if (bits == RadixSort::KeyBits::low16)
              inKeys[i] = (inKeys[i] & 65535u) | (random() & 0xffff0000u);
          }
          require(keys->upload(inKeys.data(), VkDeviceSize{n} * 4), "upload keys");
          require(values->upload(inValues.data(), VkDeviceSize{n} * 4), "upload values");
          require(count->upload(&n, 4), "upload count");
          std::stable_sort(expected.begin(), expected.end(), [&](uint32_t a, uint32_t b) {
            return (inKeys[a] & mask) < (inKeys[b] & mask);
          });
          const uint32_t slot = cases++ % RadixSort::kSlots;
          std::puts("radix stage=encode-submit-wait");
          gpu.submit([&](VkCommandBuffer cmd) {
            require(sort->encode(cmd, slot, input), "encode radix");
          });
          std::puts("radix stage=readback-metadata");
          const auto output = sort->output(slot);
          require(read(gpu, output.count, 1)[0] == n, "output count");
          require(read(gpu, output.status, 1)[0] == 0, "output status");
          std::puts("radix stage=readback-keys");
          const auto actualKeys = read(gpu, output.keys, n);
          std::puts("radix stage=readback-values");
          const auto actualValues = read(gpu, output.values, n);
          std::puts("radix stage=compare");
          require(actualValues == expected, "stable permutation differs from CPU stable_sort");
          for (uint32_t i = 0; i < n; ++i)
            require(actualKeys[i] == inKeys[expected[i]], "key/value association lost");
        }
      }
    }
    uint32_t overflow = capacity + 1;
    require(count->upload(&overflow, 4), "upload invalid count");
    gpu.submit(
        [&](VkCommandBuffer cmd) { require(sort->encode(cmd, 0, input), "encode invalid count"); });
    require(read(gpu, sort->output(0).count, 1)[0] == 0, "invalid count fails closed");
    require(read(gpu, sort->output(0).status, 1)[0] == RadixSort::kInvalidCount,
            "invalid count status");
    gpu.requireValidationClean();
    std::printf(
        "PASS radix: %u stable-sort cases, invalid inputs/count, reserve, slots; large=%d\n", cases,
        large);
    if (!large) std::puts("SKIP 3M case: set SPLATKIT_RADIX_LARGE=1");
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL radix: %s\n", error.what());
    return 1;
  }
}
