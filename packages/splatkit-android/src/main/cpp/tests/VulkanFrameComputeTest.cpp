#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>

#include "rendering/vulkan/VulkanFrameCompute.h"
#include "rendering/vulkan/VulkanShaderTypes.h"
#include "splatkit/rendering/GpuLayout.h"
#include "tests/VulkanTestContext.h"

namespace {
using namespace splatkit;
using test::require;

splat::SplatCloud cloud(uint32_t count) {
  splat::SplatCloud result;
  for (uint32_t i = 0; i < count; ++i) {
    result.positions.insert(result.positions.end(), {0, 0, -1.0f - i * 0.02f});
    result.covariances.insert(result.covariances.end(), {0.01f, 0, 0, 0.01f, 0, 0.01f});
    result.colors.insert(result.colors.end(), {1, 0, 0});
    result.alphas.push_back(0.5f);
  }
  return result;
}

struct Inputs {
  Inputs(const test::VulkanTestContext& gpu, const splat::SplatCloud& source) {
    camera = GpuBuffer::hostVisible(*gpu.context, sizeof(CameraUniform),
                                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    auto packed = packSplats(source);
    if (packed.empty()) packed.resize(1);
    splats = GpuBuffer::deviceLocal(*gpu.context, packed.size() * sizeof(GpuSplat),
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    require(camera && splats && splats->upload(packed.data(), packed.size() * sizeof(GpuSplat)),
            "frame input allocation/upload");
    setView(0);
  }
  void setView(float x) const {
    CameraUniform u{};
    u.view = splat::Mat4::identity();
    u.view.at(0, 3) = x;
    u.proj = splat::Mat4::perspective(1, 1, 0.1f, 100);
    u.screenSize[0] = u.screenSize[1] = 1000;
    u.focal[0] = u.focal[1] = 500;
    u.tanHalfFov[0] = u.tanHalfFov[1] = 1;
    std::memcpy(camera->mapped(), &u, sizeof(u));
    camera->flush(0, sizeof(u));
  }
  std::unique_ptr<GpuBuffer> camera, splats;
};

std::vector<uint32_t> run(const test::VulkanTestContext& gpu, VulkanFrameCompute& pass,
                          const Inputs& inputs, uint32_t slot, uint32_t capacity,
                          const std::vector<SplatRenderer::Range>& ranges) {
  const VkDeviceSize bytes = sizeof(VkDrawIndirectCommand) + std::max(1u, capacity) * 4ull;
  auto copy = GpuBuffer::deviceLocal(*gpu.context, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
  require(copy != nullptr, "frame result allocation");
  gpu.submit([&](VkCommandBuffer cmd) {
    SplatRenderer::Frame frame;
    frame.orderSource = SplatRenderer::OrderSource::gpu;
    frame.ranges = ranges.data();
    frame.rangeCount = static_cast<uint32_t>(ranges.size());
    auto draw = pass.encode(cmd, slot, inputs.camera->handle(), *inputs.splats, frame);
    require(draw.has_value(), "GPU frame encode");
    require(draw->capacity <= std::max(1u, capacity), "draw buffer bound");
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         1, &barrier, 0, nullptr, 0, nullptr);
    const VkBufferCopy args{0, 0, sizeof(VkDrawIndirectCommand)};
    const VkBufferCopy order{0, sizeof(VkDrawIndirectCommand), draw->capacity * 4ull};
    vkCmdCopyBuffer(cmd, draw->arguments, copy->handle(), 1, &args);
    vkCmdCopyBuffer(cmd, draw->order, copy->handle(), 1, &order);
  });
  auto data = gpu.readback(*copy, bytes);
  VkDrawIndirectCommand args{};
  std::memcpy(&args, data.data(), sizeof(args));
  require(args.vertexCount == 4 && args.instanceCount <= capacity && args.firstVertex == 0 &&
              args.firstInstance == 0,
          "GPU draw arguments bounded and initialized");
  std::vector<uint32_t> indices(args.instanceCount);
  if (!indices.empty())
    std::memcpy(indices.data(), data.data() + sizeof(args), indices.size() * sizeof(uint32_t));
  return indices;
}

void tests(const test::VulkanTestContext& gpu) {
  auto source = cloud(257);
  Inputs inputs(gpu, source);
  const std::vector<SplatRenderer::Range> ranges{{100, 40}, {0, 10}, {240, 17}};
  std::vector<uint32_t> expected;
  for (auto range : ranges)
    for (uint32_t i = 0; i < range.count; ++i) expected.push_back(range.offset + i);
  std::sort(expected.rbegin(), expected.rend());
  for (const char* bits : {"32", "16"}) {
    require(setenv("SPLATKIT_VULKAN_SORT_BITS", bits, 1) == 0, "set test precision");
    auto created = VulkanFrameCompute::create(*gpu.context, 257);
    require(bool(created), "create GPU frame");
    auto pass = std::move(created.value());
    const auto actual = run(gpu, *pass, inputs, 0, 257, ranges);
    if (actual != expected) {
      std::fprintf(stderr, "range ordering: expected %zu, got %zu\n", expected.size(),
                   actual.size());
      for (size_t i = 0; i < actual.size(); ++i)
        std::fprintf(stderr, "%zu: expected %u, got %u\n", i,
                     i < expected.size() ? expected[i] : UINT32_MAX, actual[i]);
    }
    require(actual == expected, "back-to-front range ordering");
    require(run(gpu, *pass, inputs, 1, 257, {}).empty(), "empty ranges draw nothing");
    require(run(gpu, *pass, inputs, 0, 257, ranges) == expected, "slot reuse ordering");
    require(pass->stats().drawn == expected.size() && pass->stats().status == 0,
            "delayed diagnostics match completed GPU count");
    inputs.setView(100);
    require(run(gpu, *pass, inputs, 1, 257, ranges).empty(), "offscreen ranges draw nothing");
    require(pass->stats().drawn == 0, "zero drawn remains zero with a loaded source");
    inputs.setView(0);
    require(run(gpu, *pass, inputs, 0, 257, ranges) == expected, "visibility recovers");
    gpu.submit([&](VkCommandBuffer cmd) {
      SplatRenderer::Frame frame;
      frame.orderSource = SplatRenderer::OrderSource::gpu;
      const SplatRenderer::Range overlap[]{{0, 2}, {1, 2}};
      frame.ranges = overlap;
      frame.rangeCount = 2;
      require(!pass->encode(cmd, 1, inputs.camera->handle(), *inputs.splats, frame),
              "overlapping residency ranges rejected");
      frame.rangeCount = 0;
      require(!pass->encode(cmd, 2, inputs.camera->handle(), *inputs.splats, frame),
              "invalid frame slot rejected");
    });
    std::printf("PASS %s-bit visibility/sort/indirect chain, ranges, empty view and diagnostics\n",
                bits);
  }
  require(unsetenv("SPLATKIT_VULKAN_SORT_BITS") == 0, "restore test precision");

  splat::LodTree tree;
  tree.leafCount = 2;
  tree.layout = {{{0, 0, -3}, 4, 1, 2}, {{0, 0, -1}, 0.2f, 0, 0}, {{0, 0, -5}, 0.2f, 0, 0}};
  tree.nodes = cloud(3);
  tree.nodes.positions = {0, 0, -3, 0, 0, -1, 0, 0, -5};
  tree.nodes.covariances[0] = tree.nodes.covariances[3] = tree.nodes.covariances[5] = 1;
  tree.selection = splat::buildLodSelectionData(tree);
  Inputs lodInputs(gpu, tree.nodes);
  for (uint32_t budget : {1u, 2u}) {
    auto created = VulkanFrameCompute::create(*gpu.context, 3, &tree, budget);
    require(bool(created), "create GPU LOD frame");
    auto pass = std::move(created.value());
    require(pass->hasLod(), "hierarchy is GPU selected");
    const auto cut = budget == 1 ? std::vector<uint32_t>{0} : std::vector<uint32_t>{2, 1};
    require(run(gpu, *pass, lodInputs, 0, budget, {}) == cut,
            "LOD GPU count feeds visibility/sort");
    require(run(gpu, *pass, lodInputs, 0, budget, {}) == cut, "LOD slot reuse");
    require(pass->stats().selected == budget && pass->stats().drawn == budget &&
                pass->stats().status == 0,
            "LOD completed diagnostics");
  }
  std::puts("PASS hierarchical selection feeds sorted indirect draw with covering pressure");
  gpu.requireValidationClean();
}
}  // namespace

int main() {
  try {
    test::VulkanTestContext gpu;
    std::printf("GPU: %s; validation: %s\n", gpu.context->deviceDescription().c_str(),
                gpu.context->validationEnabled() ? "on" : "off");
    tests(gpu);
    return 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL %s\n", e.what());
    return 1;
  }
}
