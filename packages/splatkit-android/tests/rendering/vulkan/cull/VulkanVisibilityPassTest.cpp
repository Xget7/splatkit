#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <numeric>
#include <set>

#include "rendering/vulkan/cull/VulkanVisibilityPass.h"
#include "rendering/vulkan/layout/VulkanShaderTypes.h"
#include "splatkit/rendering/GpuLayout.h"
#include "support/VulkanTestContext.h"

namespace {
using splatkit::CameraUniform;
using splatkit::GpuSplat;
using splatkit::packSplats;
using splatkit::VulkanBuffer;
using splatkit::VulkanVisibilityPass;
namespace test = splatkit::test;
using test::require;
using Mode = VulkanVisibilityPass::CandidateMode;

CameraUniform camera() {
  CameraUniform cam{};
  cam.view = splat::Mat4::identity();
  cam.proj = splat::Mat4::perspective(1, 1, 0.1f, 100);
  cam.focal[0] = cam.focal[1] = 500;
  cam.tanHalfFov[0] = cam.tanHalfFov[1] = 1;
  cam.screenSize[0] = cam.screenSize[1] = 1000;
  return cam;
}
std::vector<GpuSplat> source(uint32_t count) {
  splat::SplatCloud cloud;
  for (uint32_t i = 0; i < count; ++i) {
    cloud.positions.insert(cloud.positions.end(), {0, 0, -static_cast<float>(1 + i % 10)});
    cloud.covariances.insert(cloud.covariances.end(), {0.01f, 0, 0, 0.01f, 0, 0.01f});
    cloud.colors.insert(cloud.colors.end(), {1, 0, 0});
    cloud.alphas.push_back(0.5f);
  }
  return packSplats(cloud);
}
template <typename T>
std::unique_ptr<VulkanBuffer> upload(const test::VulkanTestContext& gpu,
                                     const std::vector<T>& values) {
  const size_t bytes = std::max(values.size() * sizeof(T), sizeof(T));
  auto buffer = VulkanBuffer::deviceLocal(*gpu.context, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  require(buffer != nullptr, "input allocation");
  const T zero{};
  require(buffer->upload(values.empty() ? &zero : values.data(), bytes), "input upload");
  return buffer;
}
struct Fixture {
  explicit Fixture(const test::VulkanTestContext& gpu)
      : splats(upload(gpu, source(300))),
        uniforms(VulkanBuffer::hostVisible(*gpu.context, sizeof(CameraUniform),
                                           VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)) {
    require(uniforms != nullptr, "uniform allocation");
    setCamera(camera());
  }
  void setCamera(const CameraUniform& cam) const {
    std::memcpy(uniforms->mapped(), &cam, sizeof(cam));
    uniforms->flush(0, sizeof(cam));
  }
  VulkanVisibilityPass::Input input() const {
    VulkanVisibilityPass::Input in;
    in.camera = uniforms->handle();
    in.cameraBytes = uniforms->size();
    in.splats = splats->handle();
    in.splatsBytes = splats->size();
    in.sourceCount = 300;
    return in;
  }
  std::unique_ptr<VulkanBuffer> splats, uniforms;
};
struct Result {
  uint32_t count = 0, status = 0;
  VkDrawIndirectCommand draw{};
  std::vector<uint32_t> indices, keys;
};
Result run(const test::VulkanTestContext& gpu, const VulkanVisibilityPass& pass,
           const VulkanVisibilityPass::Input& input, uint32_t slot = 0) {
  const VkDeviceSize bytes = 24 + pass.capacity() * 8ull;
  auto copy = VulkanBuffer::deviceLocal(
      *gpu.context, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  require(copy != nullptr, "readback copy allocation");
  gpu.submit([&](VkCommandBuffer cmd) {
    require(pass.encode(cmd, slot, input), "visibility encode");
    auto out = pass.output(slot);
    const VkBufferCopy count{0, 0, 4};
    const VkBufferCopy status{0, 4, 4};
    const VkBufferCopy draw{0, 8, 16};
    const VkBufferCopy indices{0, 24, pass.capacity() * 4ull};
    const VkBufferCopy keys{0, 24 + pass.capacity() * 4ull, pass.capacity() * 4ull};
    vkCmdCopyBuffer(cmd, out.count, copy->handle(), 1, &count);
    vkCmdCopyBuffer(cmd, out.status, copy->handle(), 1, &status);
    vkCmdCopyBuffer(cmd, out.indirect, copy->handle(), 1, &draw);
    vkCmdCopyBuffer(cmd, out.indices, copy->handle(), 1, &indices);
    vkCmdCopyBuffer(cmd, out.depthKeys, copy->handle(), 1, &keys);
  });
  const auto raw = gpu.readback(*copy, bytes);
  Result result;
  std::memcpy(&result.count, raw.data(), 4);
  std::memcpy(&result.status, raw.data() + 4, 4);
  std::memcpy(&result.draw, raw.data() + 8, 16);
  require(result.count <= pass.capacity(), "count within output capacity");
  require(result.draw.vertexCount == 4 && result.draw.instanceCount == result.count &&
              result.draw.firstVertex == 0 && result.draw.firstInstance == 0,
          "indirect draw agrees with normalized count");
  result.indices.resize(result.count);
  result.keys.resize(result.count);
  if (result.count) {
    std::memcpy(result.indices.data(), raw.data() + 24, result.count * 4);
    std::memcpy(result.keys.data(), raw.data() + 24 + pass.capacity() * 4ull, result.count * 4);
  }
  return result;
}
void membership(const Result& result, std::vector<uint32_t> expected) {
  require(result.status == 0, "no visibility failure");
  auto indices = result.indices;
  std::sort(indices.begin(), indices.end());
  std::sort(expected.begin(), expected.end());
  require(indices == expected, "candidate membership equals expected source indices");
}
void failed(const Result& result, uint32_t bit) {
  require((result.status & bit) != 0 && result.count == 0 && result.draw.instanceCount == 0,
          "GPU failure zeros count and indirect draw");
}
void indexed(VulkanVisibilityPass::Input& in, const VulkanBuffer& indices,
             const VulkanBuffer& count, uint32_t bound) {
  in.mode = Mode::indices;
  in.candidates = indices.handle();
  in.candidatesBytes = indices.size();
  in.candidateCount = count.handle();
  in.candidateCountBytes = count.size();
  in.candidateCapacity = bound;
}
void ranges(VulkanVisibilityPass::Input& in, const VulkanBuffer& records, uint32_t count,
            uint32_t bound) {
  in.mode = Mode::ranges;
  in.candidates = records.handle();
  in.candidatesBytes = records.size();
  in.rangeCount = count;
  in.candidateCapacity = bound;
}

using Gpu = test::VulkanTestContext;

void testPrefix(const Gpu& gpu, const VulkanVisibilityPass& pass, const Fixture& f) {
  for (const uint32_t n : {0u, 1u, 127u, 128u, 129u, 257u, 300u}) {
    auto in = f.input();
    in.sourceCount = n;
    std::vector<uint32_t> expected(n);
    std::iota(expected.begin(), expected.end(), 0);
    membership(run(gpu, pass, in), expected);
    membership(run(gpu, pass, in, 1), expected);
  }
  std::puts("PASS prefix, empty, subgroup tails and descriptor slots");
}

void testSparseIndexed(const Gpu& gpu, const VulkanVisibilityPass& pass, const Fixture& f) {
  // LOD reserves far more entries than it emits. Poison inactive groups so reading
  // a capacity tail instead of the GPU count becomes a visible diagnostic failure.
  std::vector<uint32_t> sparseIndices(4097, UINT32_MAX);
  std::iota(sparseIndices.begin(), sparseIndices.begin() + 300, 0);
  auto sparseBuffer = upload(gpu, sparseIndices);
  auto sparseCount = upload(gpu, std::vector<uint32_t>{0});
  auto sparse = f.input();
  indexed(sparse, *sparseBuffer, *sparseCount, 4097);
  for (uint32_t n : {0u, 1u, 127u, 128u, 129u, 257u}) {
    require(sparseCount->upload(&n, 4), "sparse GPU count");
    std::vector<uint32_t> expected(n);
    std::iota(expected.begin(), expected.end(), 0);
    membership(run(gpu, pass, sparse), expected);
    membership(run(gpu, pass, sparse, 1), expected);
  }
  uint32_t sparseInvalid = 4098;
  require(sparseCount->upload(&sparseInvalid, 4), "sparse invalid count");
  failed(run(gpu, pass, sparse), VulkanVisibilityPass::kInvalidCount);
  sparseInvalid = 0;
  require(sparseCount->upload(&sparseInvalid, 4), "sparse empty count");
  auto invalidProjection = camera();
  invalidProjection.proj = splat::Mat4::identity();
  f.setCamera(invalidProjection);
  sparse.keyBits = VulkanVisibilityPass::KeyBits::low16;
  failed(run(gpu, pass, sparse), VulkanVisibilityPass::kInvalidProjection);
  f.setCamera(camera());
  std::puts("PASS sparse indexed workgroups, poisoned tails and empty diagnostics");
}

void testIndexedLod(const Gpu& gpu, VulkanVisibilityPass& pass, const Fixture& f) {
  require(pass.reserve(3), "small output capacity");
  auto indexBuffer = upload(gpu, std::vector<uint32_t>{299, 0, 128});
  auto countBuffer = upload(gpu, std::vector<uint32_t>{3});
  auto in = f.input();
  indexed(in, *indexBuffer, *countBuffer, 3);
  membership(run(gpu, pass, in), {299, 0, 128});
  const uint32_t zero = 0;
  const uint32_t excessive = 4;
  require(countBuffer->upload(&zero, 4), "zero GPU count");
  membership(run(gpu, pass, in), {});
  in.candidateCapacity = 0;
  membership(run(gpu, pass, in), {});
  require(countBuffer->upload(&excessive, 4), "bad GPU count");
  failed(run(gpu, pass, in), VulkanVisibilityPass::kInvalidCount);
  in.candidateCapacity = 3;
  failed(run(gpu, pass, in), VulkanVisibilityPass::kInvalidCount);
  auto badIndex = upload(gpu, std::vector<uint32_t>{0, 300, UINT32_MAX});
  const uint32_t three = 3;
  require(countBuffer->upload(&three, 4), "restore GPU count");
  indexed(in, *badIndex, *countBuffer, 3);
  failed(run(gpu, pass, in), VulkanVisibilityPass::kInvalidIndex);
  std::puts("PASS indexed LOD, high resident indices, bad index/count");
}

void testRanges(const Gpu& gpu, VulkanVisibilityPass& pass, const Fixture& f) {
  using Range = VulkanVisibilityPass::Range;
  require(pass.reserve(5), "range output capacity");
  auto records = upload(gpu, std::vector<Range>{{2, 2, 2, 0}, {290, 3, 5, 0}});
  auto in = f.input();
  ranges(in, *records, 2, 5);
  membership(run(gpu, pass, in), {2, 3, 290, 291, 292});
  auto emptyRecords = upload(gpu, std::vector<Range>{});
  ranges(in, *emptyRecords, 0, 0);
  membership(run(gpu, pass, in), {});
  auto badRecords = upload(gpu, std::vector<Range>{{299, 2, 2, 0}});
  ranges(in, *badRecords, 1, 2);
  failed(run(gpu, pass, in), VulkanVisibilityPass::kInvalidRange);
  auto badPrefix = upload(gpu, std::vector<Range>{{2, 2, 3, 0}});
  ranges(in, *badPrefix, 1, 2);
  failed(run(gpu, pass, in), VulkanVisibilityPass::kInvalidRange);
  auto overlap = upload(gpu, std::vector<Range>{{2, 2, 2, 0}, {3, 2, 4, 0}});
  ranges(in, *overlap, 2, 4);
  failed(run(gpu, pass, in), VulkanVisibilityPass::kInvalidRange);
  auto hiddenBadRange = upload(gpu, std::vector<Range>{{0, 2, 2, 0}, {2, 1, 1, 0}, {4, 2, 3, 0}});
  ranges(in, *hiddenBadRange, 3, 3);
  failed(run(gpu, pass, in), VulkanVisibilityPass::kInvalidRange);
  std::puts("PASS streamed ranges, empty ranges and malformed ranges");
}

void testOverflow(const Gpu& gpu, const VulkanVisibilityPass& pass, const Fixture& f) {
  auto in = f.input();  // source count exceeds output capacity: GPU overflow, never CPU truncation
  failed(run(gpu, pass, in), VulkanVisibilityPass::kOverflow);
  in.candidateCapacity = 3;
  membership(run(gpu, pass, in), {0, 1, 2});
  std::puts("PASS output overflow fails closed and recovers");
}

uint32_t expectedDepthKey(float depth, VulkanVisibilityPass::KeyBits bits,
                          VulkanVisibilityPass::KeyOrder order) {
  const auto cam = camera();
  const float nearPlane = cam.proj.at(2, 3) / cam.proj.at(2, 2);
  const float farPlane = cam.proj.at(2, 3) / (cam.proj.at(2, 2) + 1);
  const bool low16 = bits == VulkanVisibilityPass::KeyBits::low16;
  uint32_t expected = 0;
  std::memcpy(&expected, &depth, 4);
  if (low16)
    expected = static_cast<uint32_t>(
        std::clamp((depth - nearPlane) / (farPlane - nearPlane), 0.0f, 1.0f) * 65535);
  if (order == VulkanVisibilityPass::KeyOrder::descending)
    expected = low16 ? 65535 - expected : ~expected;
  return expected;
}

void requireDepthKeys(const Result& result, VulkanVisibilityPass::KeyBits bits,
                      VulkanVisibilityPass::KeyOrder order) {
  for (size_t i = 0; i < result.count; ++i) {
    const auto depth = static_cast<float>(1 + result.indices[i] % 10);
    const uint32_t expected = expectedDepthKey(depth, bits, order);
    if (bits == VulkanVisibilityPass::KeyBits::full32)
      require(result.keys[i] == expected,
              "32-bit depth key preserves float bits and BTF inversion");
    else
      require(
          result.keys[i] <= 65535 && std::abs(static_cast<int64_t>(result.keys[i]) - expected) <= 1,
          "16-bit quantized depth key (one-bin floating-point tolerance)");
  }
}

void testDepthKeys(const Gpu& gpu, const VulkanVisibilityPass& pass, const Fixture& f) {
  auto in = f.input();
  in.candidateCapacity = 3;
  for (auto bits : {VulkanVisibilityPass::KeyBits::full32, VulkanVisibilityPass::KeyBits::low16}) {
    for (auto order :
         {VulkanVisibilityPass::KeyOrder::ascending, VulkanVisibilityPass::KeyOrder::descending}) {
      in.keyBits = bits;
      in.keyOrder = order;
      auto result = run(gpu, pass, in);
      membership(result, {0, 1, 2});
      requireDepthKeys(result, bits, order);
    }
  }
  auto badCamera = camera();
  badCamera.proj = splat::Mat4::identity();
  f.setCamera(badCamera);
  in.keyBits = VulkanVisibilityPass::KeyBits::low16;
  failed(run(gpu, pass, in), VulkanVisibilityPass::kInvalidProjection);
  f.setCamera(camera());
  std::puts("PASS full32/low16 ascending/BTF keys and invalid projection");
}

void testDescriptorRejection(const Gpu& gpu, VulkanVisibilityPass& pass, const Fixture& f) {
  const auto old = pass.output(0);
  require(!pass.reserve(0) && !pass.reserve(VulkanVisibilityPass::kMaxCapacity + 1),
          "reject capacity bounds");
  require(pass.output(0).indices == old.indices, "reserve failure preserves output");
  auto indexBuffer = upload(gpu, std::vector<uint32_t>{299, 0, 128});
  auto countBuffer = upload(gpu, std::vector<uint32_t>{3});
  auto in = f.input();
  in.candidateCapacity = 3;
  gpu.submit([&](VkCommandBuffer cmd) {
    auto bad = in;
    bad.splatsBytes = 32;
    require(!pass.encode(cmd, 0, bad), "reject undersized resident source descriptor");
    bad = in;
    bad.cameraBytes = 175;
    require(!pass.encode(cmd, 0, bad), "reject undersized uniform descriptor");
    bad = in;
    bad.splatsOffset = std::numeric_limits<VkDeviceSize>::max();
    require(!pass.encode(cmd, 0, bad), "reject offset overflow or misalignment");
    bad = in;
    // Fixed uint32_t underlying type permits this value; exercise enum validation.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    bad.mode = static_cast<Mode>(99);
    require(!pass.encode(cmd, 0, bad), "reject unknown mode");
    bad = in;
    indexed(bad, *indexBuffer, *countBuffer, 3);
    bad.candidatesBytes = 4;
    require(!pass.encode(cmd, 0, bad), "reject undersized candidate descriptor");
    bad.candidatesBytes = indexBuffer->size();
    bad.candidateCountBytes = 3;
    require(!pass.encode(cmd, 0, bad), "reject undersized count descriptor");
    require(!pass.encode(cmd, VulkanVisibilityPass::kSlots, in), "reject invalid slot");
    require(!pass.encode(VK_NULL_HANDLE, 0, in), "reject null command");
  });
  membership(run(gpu, pass, in), {0, 1, 2});
  std::puts("PASS descriptor rejection and transactional capacity");
}

void tests(const Gpu& gpu) {
  auto created = VulkanVisibilityPass::create(*gpu.context, 0);
  require(static_cast<bool>(created), "create subgroup visibility pass");
  auto pass = std::move(created.value());
  const Fixture f(gpu);
  require(pass->reserve(300), "reserve output");
  testPrefix(gpu, *pass, f);
  testSparseIndexed(gpu, *pass, f);
  testIndexedLod(gpu, *pass, f);
  testRanges(gpu, *pass, f);
  testOverflow(gpu, *pass, f);
  testDepthKeys(gpu, *pass, f);
  testDescriptorRejection(gpu, *pass, f);
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
