#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <set>

#include "rendering/vulkan/LodSelection.h"
#include "rendering/vulkan/VulkanShaderTypes.h"
#include "splat/lod/LodFile.h"
#include "tests/VulkanTestContext.h"

namespace {
using splatkit::CameraUniform;
using splatkit::GpuBuffer;
using splatkit::LodSelection;
namespace test = splatkit::test;
using test::require;

void attributes(splat::LodTree& tree) {
  for (const auto& node : tree.layout) {
    tree.nodes.positions.insert(tree.nodes.positions.end(), node.position, node.position + 3);
    tree.nodes.covariances.insert(tree.nodes.covariances.end(),
                                  {0.0001f, 0, 0, 0.0001f, 0, 0.0001f});
    tree.nodes.colors.insert(tree.nodes.colors.end(), {0.5f, 0.5f, 0.5f});
    tree.nodes.alphas.push_back(0.5f);
  }
  tree.selection = splat::buildLodSelectionData(tree);
}
splat::LodTree binary(uint32_t leaves) {
  splat::LodTree tree;
  tree.leafCount = leaves;
  for (uint32_t i = 0; i < 2 * leaves - 1; ++i)
    tree.layout.push_back(
        {{0, 0, -2}, 0.02f, i < leaves - 1 ? 2 * i + 1 : 0, i < leaves - 1 ? 2u : 0u});
  attributes(tree);
  return tree;
}
splat::LodTree packet(uint32_t leaves) {
  splat::LodTree tree;
  tree.leafCount = leaves;
  tree.layout.push_back({{0, 0, -2}, 0.02f, 1, leaves});
  for (uint32_t i = 0; i < leaves; ++i) tree.layout.push_back({{0, 0, -2}, 0.02f, 0, 0});
  attributes(tree);
  return tree;
}
void expand(const splat::LodTree& tree, uint32_t node, std::vector<uint32_t>& leaves) {
  require(node < tree.nodeCount(), "selected node in bounds");
  const auto& entry = tree.layout[node];
  if (!entry.childCount) leaves.push_back(node);
  for (uint32_t k = 0; k < entry.childCount; ++k) expand(tree, entry.childStart + k, leaves);
}
void covering(const splat::LodTree& tree, const std::vector<uint32_t>& cut) {
  std::vector<uint32_t> represented;
  std::vector<uint32_t> expected;
  for (auto index : cut) expand(tree, index, represented);
  expand(tree, 0, expected);
  std::sort(represented.begin(), represented.end());
  std::sort(expected.begin(), expected.end());
  require(represented == expected, "cut covers every original leaf exactly once");
}
CameraUniform camera(float distance = 0) {
  CameraUniform u{};
  u.view = splat::Mat4::identity();
  u.view.at(2, 3) = -distance;
  u.proj = splat::Mat4::perspective(1, 1, 0.1f, 1000000);
  u.focal[0] = u.focal[1] = 500;
  u.tanHalfFov[0] = u.tanHalfFov[1] = 1;
  u.screenSize[0] = u.screenSize[1] = 1000;
  u.cameraPosition[2] = distance;
  return u;
}
struct Selection {
  std::vector<uint32_t> cut;
  std::array<uint32_t, 6> stats{};
};
Selection select(const test::VulkanTestContext& gpu, const LodSelection& lod,
                 const CameraUniform& cam, bool doubleFrame = false) {
  auto uniforms =
      GpuBuffer::hostVisible(*gpu.context, sizeof(cam), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
  require(uniforms != nullptr, "camera allocation");
  std::memcpy(uniforms->mapped(), &cam, sizeof(cam));
  uniforms->flush(0, sizeof(cam));
  auto output = lod.output();
  const VkDeviceSize bytes = LodSelection::kDiagnosticBytes + output.capacity * 4ull;
  auto copy = GpuBuffer::deviceLocal(
      *gpu.context, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
  require(copy != nullptr, "result copy allocation");
  gpu.submit([&](VkCommandBuffer cmd) {
    require(lod.encode(cmd, 0, {uniforms->handle(), 0}), "encode selection");
    const VkBufferCopy state{0, 0, LodSelection::kDiagnosticBytes};
    const VkBufferCopy indices{0, LodSelection::kDiagnosticBytes, output.capacity * 4ull};
    vkCmdCopyBuffer(cmd, output.state, copy->handle(), 1, &state);
    vkCmdCopyBuffer(cmd, output.indices, copy->handle(), 1, &indices);
    if (doubleFrame) {
      // Consume first frame, then overwrite shared scratch on the same queue. Separate slots
      // avoid updating a descriptor set referenced by the earlier recorded dispatch.
      require(lod.encode(cmd, 1, {uniforms->handle(), 0}), "encode repeated frame");
      VkMemoryBarrier reuse{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
      reuse.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      reuse.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                           1, &reuse, 0, nullptr, 0, nullptr);
      vkCmdCopyBuffer(cmd, output.state, copy->handle(), 1, &state);
      vkCmdCopyBuffer(cmd, output.indices, copy->handle(), 1, &indices);
    }
  });
  const auto raw = gpu.readback(*copy, bytes);
  Selection result;
  std::memcpy(result.stats.data(), raw.data(), LodSelection::kDiagnosticBytes);
  require(result.stats[0] <= output.capacity, "selected count bounded");
  result.cut.resize(result.stats[0]);
  if (!result.cut.empty())
    std::memcpy(result.cut.data(), raw.data() + LodSelection::kDiagnosticBytes,
                result.cut.size() * 4);
  return result;
}

void tests(const test::VulkanTestContext& gpu) {
  auto created = LodSelection::create(*gpu.context);
  require(static_cast<bool>(created), "LOD pipeline creation");
  auto lod = std::move(created.value());
  const auto tree = binary(512);
  for (const uint32_t capacity : {1u, 7u, 127u, 128u, 129u, 257u, 512u}) {
    require(lod->upload(tree, capacity, {0, 4, false}), "binary hierarchy upload");
    auto result = select(gpu, *lod, camera());
    covering(tree, result.cut);
    require(result.cut.size() == capacity, "binary capacity fully used for exact refinement");
    require(result.cut == select(gpu, *lod, camera(), true).cut, "repeated frames deterministic");
    require((result.stats[4] > 0) == (capacity < 512), "denied refinements reported");
    if (capacity == 512) {
      require(result.stats[5] == tree.selection.clusters.size(), "only interiors evaluated");
      for (auto node : result.cut) require(tree.layout[node].childCount == 0, "exact leaves");
    }
  }
  std::puts("PASS covering cuts, exact leaves, pressure, repeated frames");

  require(lod->upload(tree, 512, {0, 4, true}), "culled hierarchy upload");
  require(select(gpu, *lod, camera(-100)).cut.empty(), "empty view");
  require(!select(gpu, *lod, camera()).cut.empty(), "empty view recovers");
  auto single = binary(1);
  require(lod->upload(single, 1), "single-leaf root upload");
  require(select(gpu, *lod, camera()).cut == std::vector<uint32_t>{0}, "single-leaf root");
  std::puts("PASS empty view and singleton");

  auto fan = packet(513);
  require(lod->upload(fan, 513, {0, 4, false}), "leaf packet upload");
  auto full = select(gpu, *lod, camera());
  covering(fan, full.cut);
  require(full.cut.size() == 513 && full.stats[5] == 1 && full.stats[4] == 0,
          "packet emitted cooperatively");
  require(lod->upload(fan, 512, {0, 4, false}), "packet pressure upload");
  auto denied = select(gpu, *lod, camera());
  require(denied.cut == std::vector<uint32_t>{0} && denied.stats[4] == 1,
          "whole packet denied without holes");
  std::puts("PASS large leaf packet and pressure");

  auto old = lod->output();
  require(!lod->upload(fan, 0), "reject zero capacity");
  require(!lod->upload(fan, UINT32_MAX), "reject excessive capacity");
  require(!lod->upload(fan, 513, {-1, 4, false}), "reject negative error threshold");
  require(!lod->upload(fan, 513, {1, std::numeric_limits<float>::quiet_NaN(), false}),
          "reject NaN quality");
  auto invalid = fan;
  invalid.selection.clusters[0].leafCount++;
  require(!lod->upload(invalid, 513), "reject invalid metadata");
  require(lod->output().indices == old.indices && lod->output().state == old.state &&
              lod->capacity() == old.capacity,
          "failed upload is transactional");
  require(select(gpu, *lod, camera()).cut == denied.cut, "old world survives failed upload");
  require(!lod->encode(VK_NULL_HANDLE, 0, {}), "reject null command");
  gpu.submit([&](VkCommandBuffer cmd) {
    require(!lod->encode(cmd, LodSelection::kSlots, {}), "reject invalid slot");
    require(!lod->encode(cmd, 0, {}), "reject null camera");
  });
  std::puts("PASS invalid inputs and transactional upload");

  splat::LodTree appearance;
  appearance.leafCount = 4;
  appearance.layout = {{{0, 0, -2}, 2, 1, 2},        {{-0.7f, 0, -2}, 0.1f, 3, 2},
                       {{0.7f, 0, -2}, 1.8f, 5, 2},  {{-0.8f, 0, -2}, 0.1f, 0, 0},
                       {{-0.6f, 0, -2}, 0.1f, 0, 0}, {{0.6f, 0, -2}, 0.1f, 0, 0},
                       {{0.8f, 0, -2}, 0.1f, 0, 0}};
  attributes(appearance);
  for (float& color : appearance.nodes.colors) color = 1;
  for (size_t i = 0; i < appearance.nodeCount(); ++i)
    for (const uint32_t axis : {0u, 3u, 5u}) appearance.nodes.covariances[i * 6 + axis] = 0.01f;
  appearance.nodes.colors[5 * 3] = 0;
  appearance.nodes.colors[6 * 3 + 1] = 0;
  appearance.selection = splat::buildLodSelectionData(appearance);
  require(lod->upload(appearance, 4, {1, 4, false}), "appearance upload");
  auto mixed = select(gpu, *lod, camera(100));
  require(std::set<uint32_t>(mixed.cut.begin(), mixed.cut.end()) == std::set<uint32_t>{1, 5, 6},
          "appearance SSE matches Metal reference cut");
  require(mixed.stats[4] == 0 && mixed.stats[5] == 3, "quality target avoids filling capacity");
  require(select(gpu, *lod, camera(1000000)).cut == std::vector<uint32_t>{0},
          "distance reduces refinement");
  std::puts("PASS appearance SSE and distance");

  // 32768 active interior nodes -> 256 workgroup totals -> two scan blocks.
  const auto large = binary(65536);
  for (const uint32_t capacity : {40000u, 65536u}) {
    require(lod->upload(large, capacity, {0, 4, false}), "multiblock upload");
    auto result = select(gpu, *lod, camera());
    covering(large, result.cut);
    require(result.cut.size() == capacity, "multiblock exact count");
    require(result.cut == select(gpu, *lod, camera()).cut, "multiblock deterministic");
  }
  std::puts("PASS multiblock prefix scans");
  auto legacy = tree;
  legacy.selection = {};
  require(lod->upload(legacy, 512, {0, 4, false}), "v1 upload metadata construction");
  covering(legacy, select(gpu, *lod, camera()).cut);
  std::puts("PASS v1 metadata compatibility");
}
}  // namespace

int main() {
  try {
    test::VulkanTestContext gpu;
    std::printf("GPU: %s\n", gpu.context->deviceDescription().c_str());
    tests(gpu);
    gpu.requireValidationClean();
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
}
