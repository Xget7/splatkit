#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <numeric>
#include <set>

#include "rendering/vulkan/VisibilityPass.h"
#include "rendering/vulkan/VulkanShaderTypes.h"
#include "splatkit/rendering/GpuLayout.h"
#include "tests/VulkanTestContext.h"

namespace {
using splatkit::CameraUniform;
using splatkit::GpuBuffer;
using splatkit::GpuSplat;
using splatkit::packSplats;
using splatkit::VisibilityPass;
namespace test = splatkit::test;
using test::require;
using Mode = VisibilityPass::CandidateMode;

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
std::unique_ptr<GpuBuffer> upload(const test::VulkanTestContext& gpu,
                                  const std::vector<T>& values) {
  const size_t bytes = std::max(values.size() * sizeof(T), sizeof(T));
  auto buffer = GpuBuffer::deviceLocal(*gpu.context, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  require(buffer != nullptr, "input allocation");
  const T zero{};
  require(buffer->upload(values.empty() ? &zero : values.data(), bytes), "input upload");
  return buffer;
}
struct Fixture {
  explicit Fixture(const test::VulkanTestContext& gpu)
      : splats(upload(gpu, source(300))),
        uniforms(GpuBuffer::hostVisible(*gpu.context, sizeof(CameraUniform),
                                        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)) {
    require(uniforms != nullptr, "uniform allocation");
    setCamera(camera());
  }
  void setCamera(const CameraUniform& cam) const {
    std::memcpy(uniforms->mapped(), &cam, sizeof(cam));
    uniforms->flush(0, sizeof(cam));
  }
  VisibilityPass::Input input() const {
    VisibilityPass::Input in;
    in.camera = uniforms->handle();
    in.cameraBytes = uniforms->size();
    in.splats = splats->handle();
    in.splatsBytes = splats->size();
    in.sourceCount = 300;
    return in;
  }
  std::unique_ptr<GpuBuffer> splats, uniforms;
};
struct Result {
  uint32_t count = 0, status = 0;
  VkDrawIndirectCommand draw{};
  std::vector<uint32_t> indices, keys;
};
Result run(const test::VulkanTestContext& gpu, const VisibilityPass& pass,
           const VisibilityPass::Input& input, uint32_t slot = 0) {
  const VkDeviceSize bytes = 24 + pass.capacity() * 8ull;
  auto copy = GpuBuffer::deviceLocal(
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
void indexed(VisibilityPass::Input& in, const GpuBuffer& indices, const GpuBuffer& count,
             uint32_t bound) {
  in.mode = Mode::indices;
  in.candidates = indices.handle();
  in.candidatesBytes = indices.size();
  in.candidateCount = count.handle();
  in.candidateCountBytes = count.size();
  in.candidateCapacity = bound;
}
void ranges(VisibilityPass::Input& in, const GpuBuffer& records, uint32_t count, uint32_t bound) {
  in.mode = Mode::ranges;
  in.candidates = records.handle();
  in.candidatesBytes = records.size();
  in.rangeCount = count;
  in.candidateCapacity = bound;
}

void tests(const test::VulkanTestContext& gpu) {
  auto created = VisibilityPass::create(*gpu.context, 0);
  require(static_cast<bool>(created), "create subgroup visibility pass");
  auto pass = std::move(created.value());
  const Fixture f(gpu);
  require(pass->reserve(300), "reserve output");
  for (const uint32_t n : {0u, 1u, 127u, 128u, 129u, 257u, 300u}) {
    auto in = f.input();
    in.sourceCount = n;
    std::vector<uint32_t> expected(n);
    std::iota(expected.begin(), expected.end(), 0);
    membership(run(gpu, *pass, in), expected);
    membership(run(gpu, *pass, in, 1), expected);
  }
  std::puts("PASS prefix, empty, subgroup tails and descriptor slots");

  require(pass->reserve(3), "small output capacity");
  auto indexBuffer = upload(gpu, std::vector<uint32_t>{299, 0, 128});
  auto countBuffer = upload(gpu, std::vector<uint32_t>{3});
  auto in = f.input();
  indexed(in, *indexBuffer, *countBuffer, 3);
  membership(run(gpu, *pass, in), {299, 0, 128});
  const uint32_t zero = 0;
  const uint32_t excessive = 4;
  require(countBuffer->upload(&zero, 4), "zero GPU count");
  membership(run(gpu, *pass, in), {});
  in.candidateCapacity = 0;
  membership(run(gpu, *pass, in), {});
  require(countBuffer->upload(&excessive, 4), "bad GPU count");
  failed(run(gpu, *pass, in), VisibilityPass::kInvalidCount);
  in.candidateCapacity = 3;
  failed(run(gpu, *pass, in), VisibilityPass::kInvalidCount);
  auto badIndex = upload(gpu, std::vector<uint32_t>{0, 300, UINT32_MAX});
  const uint32_t three = 3;
  require(countBuffer->upload(&three, 4), "restore GPU count");
  indexed(in, *badIndex, *countBuffer, 3);
  failed(run(gpu, *pass, in), VisibilityPass::kInvalidIndex);
  std::puts("PASS indexed LOD, high resident indices, bad index/count");

  require(pass->reserve(5), "range output capacity");
  auto records = upload(gpu, std::vector<VisibilityPass::Range>{{2, 2, 2, 0}, {290, 3, 5, 0}});
  in = f.input();
  ranges(in, *records, 2, 5);
  membership(run(gpu, *pass, in), {2, 3, 290, 291, 292});
  auto emptyRecords = upload(gpu, std::vector<VisibilityPass::Range>{});
  ranges(in, *emptyRecords, 0, 0);
  membership(run(gpu, *pass, in), {});
  auto badRecords = upload(gpu, std::vector<VisibilityPass::Range>{{299, 2, 2, 0}});
  ranges(in, *badRecords, 1, 2);
  failed(run(gpu, *pass, in), VisibilityPass::kInvalidRange);
  auto badPrefix = upload(gpu, std::vector<VisibilityPass::Range>{{2, 2, 3, 0}});
  ranges(in, *badPrefix, 1, 2);
  failed(run(gpu, *pass, in), VisibilityPass::kInvalidRange);
  auto overlap = upload(gpu, std::vector<VisibilityPass::Range>{{2, 2, 2, 0}, {3, 2, 4, 0}});
  ranges(in, *overlap, 2, 4);
  failed(run(gpu, *pass, in), VisibilityPass::kInvalidRange);
  auto hiddenBadRange =
      upload(gpu, std::vector<VisibilityPass::Range>{{0, 2, 2, 0}, {2, 1, 1, 0}, {4, 2, 3, 0}});
  ranges(in, *hiddenBadRange, 3, 3);
  failed(run(gpu, *pass, in), VisibilityPass::kInvalidRange);
  std::puts("PASS streamed ranges, empty ranges and malformed ranges");

  in = f.input();  // source count exceeds output capacity: GPU overflow, never CPU truncation
  failed(run(gpu, *pass, in), VisibilityPass::kOverflow);
  in.candidateCapacity = 3;
  membership(run(gpu, *pass, in), {0, 1, 2});
  std::puts("PASS output overflow fails closed and recovers");

  for (auto bits : {VisibilityPass::KeyBits::full32, VisibilityPass::KeyBits::low16}) {
    for (auto order : {VisibilityPass::KeyOrder::ascending, VisibilityPass::KeyOrder::descending}) {
      in.keyBits = bits;
      in.keyOrder = order;
      auto result = run(gpu, *pass, in);
      membership(result, {0, 1, 2});
      const auto cam = camera();
      const float nearPlane = cam.proj.at(2, 3) / cam.proj.at(2, 2);
      const float farPlane = cam.proj.at(2, 3) / (cam.proj.at(2, 2) + 1);
      for (size_t i = 0; i < result.count; ++i) {
        const auto depth = static_cast<float>(1 + result.indices[i] % 10);
        uint32_t expected = 0;
        std::memcpy(&expected, &depth, 4);
        if (bits == VisibilityPass::KeyBits::low16)
          expected = static_cast<uint32_t>(
              std::clamp((depth - nearPlane) / (farPlane - nearPlane), 0.0f, 1.0f) * 65535);
        if (order == VisibilityPass::KeyOrder::descending)
          expected = bits == VisibilityPass::KeyBits::low16 ? 65535 - expected : ~expected;
        if (bits == VisibilityPass::KeyBits::full32)
          require(result.keys[i] == expected,
                  "32-bit depth key preserves float bits and BTF inversion");
        else
          require(result.keys[i] <= 65535 &&
                      std::abs(static_cast<int64_t>(result.keys[i]) - expected) <= 1,
                  "16-bit quantized depth key (one-bin floating-point tolerance)");
      }
    }
  }
  auto badCamera = camera();
  badCamera.proj = splat::Mat4::identity();
  f.setCamera(badCamera);
  in.keyBits = VisibilityPass::KeyBits::low16;
  failed(run(gpu, *pass, in), VisibilityPass::kInvalidProjection);
  f.setCamera(camera());
  std::puts("PASS full32/low16 ascending/BTF keys and invalid projection");

  const auto old = pass->output(0);
  require(!pass->reserve(0) && !pass->reserve(VisibilityPass::kMaxCapacity + 1),
          "reject capacity bounds");
  require(pass->output(0).indices == old.indices, "reserve failure preserves output");
  in = f.input();
  in.candidateCapacity = 3;
  gpu.submit([&](VkCommandBuffer cmd) {
    auto bad = in;
    bad.splatsBytes = 32;
    require(!pass->encode(cmd, 0, bad), "reject undersized resident source descriptor");
    bad = in;
    bad.cameraBytes = 175;
    require(!pass->encode(cmd, 0, bad), "reject undersized uniform descriptor");
    bad = in;
    bad.splatsOffset = std::numeric_limits<VkDeviceSize>::max();
    require(!pass->encode(cmd, 0, bad), "reject offset overflow or misalignment");
    bad = in;
    // Fixed uint32_t underlying type permits this value; exercise enum validation.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    bad.mode = static_cast<Mode>(99);
    require(!pass->encode(cmd, 0, bad), "reject unknown mode");
    bad = in;
    indexed(bad, *indexBuffer, *countBuffer, 3);
    bad.candidatesBytes = 4;
    require(!pass->encode(cmd, 0, bad), "reject undersized candidate descriptor");
    bad.candidatesBytes = indexBuffer->size();
    bad.candidateCountBytes = 3;
    require(!pass->encode(cmd, 0, bad), "reject undersized count descriptor");
    require(!pass->encode(cmd, VisibilityPass::kSlots, in), "reject invalid slot");
    require(!pass->encode(VK_NULL_HANDLE, 0, in), "reject null command");
  });
  membership(run(gpu, *pass, in), {0, 1, 2});
  std::puts("PASS descriptor rejection and transactional capacity");
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
