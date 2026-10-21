#include <cstdio>
#include <cstring>
#include <vector>

#include "rendering/vulkan/world/VulkanWorld.h"
#include "splatkit/rendering/GpuLayout.h"
#include "support/VulkanTestContext.h"

namespace {
using splatkit::kFramesInFlight;
using splatkit::shStride;
using splatkit::VulkanWorld;
namespace test = splatkit::test;
using test::require;

// Spherical harmonics coefficients per channel at degree 3.
constexpr size_t kDegree3Coefficients = 15;

splat::SplatCloud cloud(uint32_t count, int shDegree = 0, size_t shValues = 0) {
  splat::SplatCloud result;
  for (uint32_t i = 0; i < count; ++i) {
    result.positions.insert(result.positions.end(), {0, 0, -1.0f - i * 0.02f});
    result.covariances.insert(result.covariances.end(), {0.01f, 0, 0, 0.01f, 0, 0.01f});
    result.colors.insert(result.colors.end(), {1, 0, 0});
    result.alphas.push_back(0.5f);
  }
  result.shDegree = shDegree;
  result.sh.assign(shValues * count, 0.25f);
  return result;
}

splat::SplatCloud degree3Cloud(uint32_t count) {
  return cloud(count, 3, 3 * kDegree3Coefficients);
}

uint64_t shBytes(uint32_t count, int degree) {
  return uint64_t{count} * shStride(degree) * sizeof(uint32_t);
}

void uploadShape(const test::VulkanTestContext& gpu) {
  auto world = VulkanWorld::upload(*gpu.context, cloud(3), 3, true);
  require(world && world->count == 3 && world->shDegree == 0, "upload a degree 0 cloud");
  require(world->splats && world->order && world->sh, "upload creates the three buffers");
  require(world->sh->size() == sizeof(uint32_t), "degree 0 keeps a one uint SH placeholder");
  for (const auto& staging : world->orderStaging)
    require(staging && staging->size() == 3 * sizeof(uint32_t), "CPU order staging per slot");

  world = VulkanWorld::upload(*gpu.context, cloud(3), 3, false);
  require(world && world->order->size() == sizeof(uint32_t), "GPU order keeps a one uint order");
  for (const auto& staging : world->orderStaging) require(!staging, "GPU order has no staging");

  world = VulkanWorld::upload(*gpu.context, cloud(0), 3, true);
  require(world && world->count == 0 && world->splats->size() == sizeof(splatkit::GpuSplat),
          "an empty cloud still gets valid buffers");

  splat::SplatCloud broken = cloud(3);
  broken.alphas.pop_back();
  require(!VulkanWorld::upload(*gpu.context, broken, 3, true), "reject mismatched attributes");
}

void uploadShDegree(const test::VulkanTestContext& gpu) {
  const splat::SplatCloud source = degree3Cloud(5);
  const struct {
    int limit;
    int expected;
  } cases[] = {{3, 3}, {1, 1}, {0, 0}, {9, 3}};
  for (const auto& c : cases) {
    auto world = VulkanWorld::upload(*gpu.context, source, c.limit, true);
    require(world && world->shDegree == c.expected, "SH degree follows the limit");
    if (c.expected > 0)
      require(world->sh->size() == shBytes(5, c.expected), "SH buffer holds the kept bands");
  }
  auto world = VulkanWorld::upload(*gpu.context, cloud(5, 2, 0), 3, true);
  require(world && world->shDegree == 0, "a cloud that claims SH without coefficients has none");
}

void slabAndTiles(const test::VulkanTestContext& gpu) {
  require(!VulkanWorld::createSlab(*gpu.context, 0, 1, true), "reject an empty slab");
  auto wide = VulkanWorld::createSlab(*gpu.context, 16, 7, true);
  require(wide && wide->count == 16 && wide->shDegree == 3, "slab degree is clamped to 3");
  require(wide->sh->size() == shBytes(16, 3), "slab SH buffer covers every record");
  auto narrow = VulkanWorld::createSlab(*gpu.context, 16, -2, false);
  require(narrow && narrow->shDegree == 0 && narrow->sh->size() == sizeof(uint32_t),
          "negative slab degree becomes 0");
  require(!narrow->orderStaging[0], "GPU order slab has no staging");

  auto slab = VulkanWorld::createSlab(*gpu.context, 8, 1, true);
  require(slab != nullptr, "create slab");
  require(slab->uploadTile(5, cloud(3)), "tile that ends at the capacity fits");
  require(!slab->uploadTile(6, cloud(3)), "tile past the capacity is rejected");
  require(!slab->uploadTile(9, cloud(1)), "tile starting past the capacity is rejected");
  require(slab->uploadTile(9, cloud(0)), "an empty tile is accepted anywhere");
  require(slab->uploadTile(0, degree3Cloud(2)), "tile with more bands than the slab");
  require(slab->uploadTile(2, cloud(2, 1, 3 * 3)), "tile with exactly the slab's bands");
}

// Where the GPU shares memory with the CPU, a tile is packed straight into the slab.
void tilesInSharedMemory(const test::VulkanTestContext& gpu) {
  if (!gpu.sharesDeviceMemory()) {
    std::puts("SKIP tiles in shared memory (separate device memory)");
    return;
  }
  auto slab = VulkanWorld::createSlab(*gpu.context, 8, 1, false);
  require(slab && slab->splats->mapped() && slab->sh->mapped(), "a shared-memory slab is mapped");
  const splat::SplatCloud tile = cloud(3, 1, 3 * 3);
  require(slab->uploadTile(5, tile), "tile into a mapped slab");
  const std::vector<splatkit::GpuSplat> packed = splatkit::packSplats(tile);
  const auto* splats = static_cast<const splatkit::GpuSplat*>(slab->splats->mapped());
  require(std::memcmp(splats + 5, packed.data(), packed.size() * sizeof(packed[0])) == 0,
          "the tile's records sit at its offset");
  const size_t stride = shStride(1);
  const std::vector<uint32_t> sh = splatkit::packSh(tile, 1);
  const auto* slabSh = static_cast<const uint32_t*>(slab->sh->mapped());
  require(std::memcmp(slabSh + 5 * stride, sh.data(), sh.size() * sizeof(uint32_t)) == 0,
          "the tile's harmonics sit at its offset");
  require(slab->uploadTile(5, cloud(3)), "a tile without harmonics over one with them");
  for (size_t i = 5 * stride; i < 8 * stride; ++i)
    require(slabSh[i] == 0, "a tile without harmonics zeroes its range");
}

void orderUpload(const test::VulkanTestContext& gpu) {
  auto world = VulkanWorld::upload(*gpu.context, cloud(4), 0, true);
  require(world != nullptr, "upload for order recording");
  const std::vector<uint32_t> order{3, 2, 1, 0, 9, 9};
  gpu.submit([&](VkCommandBuffer cmd) { world->recordOrderUpload(cmd, 1, order.data(), 4); });
  require(std::memcmp(world->orderStaging[1]->mapped(), order.data(), 4 * sizeof(uint32_t)) == 0,
          "the order lands in the slot's staging buffer");
  // Staging is exactly the world's size, so an uncut copy would write past it where only the
  // validation layers see it; requireValidationClean at the end covers the cut.
  gpu.submit([&](VkCommandBuffer cmd) { world->recordOrderUpload(cmd, 0, order.data(), 6); });
  require(std::memcmp(world->orderStaging[0]->mapped(), order.data(), 4 * sizeof(uint32_t)) == 0,
          "an order longer than the world still stages the world's entries");
  gpu.submit([&](VkCommandBuffer cmd) { world->recordOrderUpload(cmd, 0, order.data(), 0); });

  auto gpuOrder = VulkanWorld::upload(*gpu.context, cloud(4), 0, false);
  require(gpuOrder != nullptr, "upload with GPU order");
  gpu.submit([&](VkCommandBuffer cmd) { gpuOrder->recordOrderUpload(cmd, 0, order.data(), 4); });
}
}  // namespace

int main() {
  try {
    test::VulkanTestContext gpu;
    std::printf("GPU: %s; validation: %s\n", gpu.context->deviceDescription().c_str(),
                gpu.context->validationEnabled() ? "on" : "off");
    static_assert(kFramesInFlight >= 2, "the order test uses slots 0 and 1");
    uploadShape(gpu);
    std::puts("PASS world upload shape");
    uploadShDegree(gpu);
    std::puts("PASS world SH degree");
    slabAndTiles(gpu);
    std::puts("PASS slab and tiles");
    tilesInSharedMemory(gpu);
    std::puts("PASS tiles in shared memory");
    orderUpload(gpu);
    std::puts("PASS order upload");
    gpu.requireValidationClean();
    return 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL: %s\n", e.what());
    return 1;
  }
}
