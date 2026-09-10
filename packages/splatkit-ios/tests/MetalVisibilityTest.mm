#import <Metal/Metal.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <random>
#include <vector>

#include "SplatShaderSource.h"
#include "rendering/MetalVisibility.h"
#include "splat/math/Mat4.h"
#include "splatkit/rendering/GpuLayout.h"

namespace splatkit {
namespace {

// The device and the library once; every test encodes its own command buffer.
struct Gpu {
  id<MTLDevice> device = MTLCreateSystemDefaultDevice();
  id<MTLCommandQueue> queue = [device newCommandQueue];
  id<MTLLibrary> library = [device newLibraryWithSource:@(SplatShaderSource)
                                                options:nil
                                                  error:nil];
  static Gpu& get() {
    static Gpu gpu;
    return gpu;
  }
};

struct Pair {
  uint32_t key;
  uint32_t value;
  bool operator==(const Pair& o) const { return key == o.key && value == o.value; }
};

// Fills the key and value buffers with `pairs`, sorts on the GPU, returns what came out.
std::vector<Pair> sortOnGpu(MetalVisibility& v, const std::vector<Pair>& pairs) {
  Gpu& gpu = Gpu::get();
  auto* keys = static_cast<uint32_t*>(v.keys().contents);
  auto* values = static_cast<uint32_t*>(v.values().contents);
  for (size_t i = 0; i < pairs.size(); ++i) {
    keys[i] = pairs[i].key;
    values[i] = pairs[i].value;
  }
  *static_cast<uint32_t*>(v.countBuffer(0).contents) = static_cast<uint32_t>(pairs.size());
  id<MTLCommandBuffer> cmd = [gpu.queue commandBuffer];
  v.encodeSort(cmd, 0);
  [cmd commit];
  [cmd waitUntilCompleted];
  std::vector<Pair> out(pairs.size());
  for (size_t i = 0; i < pairs.size(); ++i) {
    out[i] = {static_cast<const uint32_t*>(v.keys().contents)[i],
              static_cast<const uint32_t*>(v.order().contents)[i]};
  }
  return out;
}

std::vector<Pair> sortOnCpu(std::vector<Pair> pairs) {
  std::stable_sort(pairs.begin(), pairs.end(),
                   [](const Pair& a, const Pair& b) { return a.key < b.key; });
  return pairs;
}

std::vector<Pair> randomPairs(size_t n, uint32_t keyMask, unsigned seed) {
  std::mt19937 rng(seed);
  std::vector<Pair> pairs(n);
  for (size_t i = 0; i < n; ++i) pairs[i] = {rng() & keyMask, static_cast<uint32_t>(i)};
  return pairs;
}

class MetalVisibilityTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_NE(Gpu::get().device, nil);
    ASSERT_NE(Gpu::get().library, nil);
    ASSERT_TRUE(visibility.create(Gpu::get().device, Gpu::get().library));
  }
  MetalVisibility visibility;
};

TEST_F(MetalVisibilityTest, SortsRandomKeysLikeAStableCpuSort) {
  const size_t n = 1000003;  // many blocks, a partial last one
  ASSERT_TRUE(visibility.reserve(n));
  const auto pairs = randomPairs(n, 0xffffffffu, 1);
  EXPECT_EQ(sortOnGpu(visibility, pairs), sortOnCpu(pairs));
}

TEST_F(MetalVisibilityTest, KeepsTheOrderOfEqualKeys) {
  const size_t n = 70000;  // few distinct keys: long runs of ties across blocks
  ASSERT_TRUE(visibility.reserve(n));
  const auto pairs = randomPairs(n, 0x7u, 2);
  EXPECT_EQ(sortOnGpu(visibility, pairs), sortOnCpu(pairs));
}

TEST_F(MetalVisibilityTest, SortsSmallAndEmptyInputs) {
  ASSERT_TRUE(visibility.reserve(64));
  EXPECT_EQ(sortOnGpu(visibility, {}), std::vector<Pair>{});
  const std::vector<Pair> one{{5, 9}};
  EXPECT_EQ(sortOnGpu(visibility, one), one);
  const auto pairs = randomPairs(17, 0xffffffffu, 3);
  EXPECT_EQ(sortOnGpu(visibility, pairs), sortOnCpu(pairs));
}

TEST_F(MetalVisibilityTest, SortsExactlyOneBlock) {
  const size_t n = MetalVisibility::kBlock;
  ASSERT_TRUE(visibility.reserve(n));
  const auto pairs = randomPairs(n, 0xffffffffu, 4);
  EXPECT_EQ(sortOnGpu(visibility, pairs), sortOnCpu(pairs));
}

// Not a check, a number: the GPU time of a sort at the scale a phone draws.
TEST_F(MetalVisibilityTest, ReportsTheSortTimeOfFiveMillionKeys) {
  const size_t n = 5000000;
  ASSERT_TRUE(visibility.reserve(n));
  const auto pairs = randomPairs(n, 0xffffffffu, 5);
  auto* keys = static_cast<uint32_t*>(visibility.keys().contents);
  auto* values = static_cast<uint32_t*>(visibility.values().contents);
  for (size_t i = 0; i < n; ++i) {
    keys[i] = pairs[i].key;
    values[i] = pairs[i].value;
  }
  *static_cast<uint32_t*>(visibility.countBuffer(0).contents) = static_cast<uint32_t>(n);
  double best = 1e9;
  for (int i = 0; i < 3; ++i) {
    id<MTLCommandBuffer> cmd = [Gpu::get().queue commandBuffer];
    visibility.encodeSort(cmd, 0);
    [cmd commit];
    [cmd waitUntilCompleted];
    best = std::min(best, (cmd.GPUEndTime - cmd.GPUStartTime) * 1000.0);
    // The result is in place after an even number of passes: sort it again as is.
  }
  const auto* sorted = static_cast<const uint32_t*>(visibility.keys().contents);
  EXPECT_TRUE(std::is_sorted(sorted, sorted + n));
  printf("[ sort     ] %zu keys: %.2f ms on %s\n", n, best,
         Gpu::get().device.name.UTF8String);
}

// The Camera block of the shader, as the renderer writes it.
struct CameraUniform {
  splat::Mat4 view;
  splat::Mat4 proj;
  float focal[2];
  float tanHalfFov[2];
  float screenSize[2];
  uint32_t outputLinear;
  uint32_t pad;
  float cameraPosition[4];
};

TEST_F(MetalVisibilityTest, CullsAndOrdersTheRangesBackToFront) {
  Gpu& gpu = Gpu::get();
  // Six splats: two ranges of three. The camera at the origin looks down -z.
  // 0: 10 m ahead, 1: behind, 2: 2 m ahead, 3: far to the side (outside the view),
  // 4: 5 m ahead, 5: never in a range.
  const float positions[6][3] = {{0, 0, -10}, {0, 0, 5}, {0, 0, -2},
                                 {50, 0, -1}, {0, 0, -5}, {0, 0, -3}};
  std::vector<GpuSplat> splats(6);
  for (int i = 0; i < 6; ++i) {
    splats[i].position[0] = positions[i][0];
    splats[i].position[1] = positions[i][1];
    splats[i].position[2] = positions[i][2];
  }
  id<MTLBuffer> splatBuffer = [gpu.device newBufferWithBytes:splats.data()
                                                      length:splats.size() * sizeof(GpuSplat)
                                                     options:MTLResourceStorageModeShared];
  CameraUniform u{};
  u.view = splat::Mat4::identity();
  u.proj = splat::Mat4::perspective(1.0f, 1.0f, 0.1f, 100.0f);
  id<MTLBuffer> uniforms = [gpu.device newBufferWithBytes:&u
                                                   length:sizeof(u)
                                                  options:MTLResourceStorageModeShared];
  ASSERT_TRUE(visibility.reserve(6));
  const SplatRenderer::Range ranges[2] = {{0, 3}, {3, 2}};

  id<MTLCommandBuffer> cmd = [gpu.queue commandBuffer];
  visibility.encode(cmd, 1, uniforms, splatBuffer, ranges, 2);
  [cmd commit];
  [cmd waitUntilCompleted];

  ASSERT_EQ(visibility.count(1), 3u);
  const auto* order = static_cast<const uint32_t*>(visibility.order().contents);
  EXPECT_EQ(std::vector<uint32_t>(order, order + 3), (std::vector<uint32_t>{0, 4, 2}));
  const auto* draw = static_cast<const uint32_t*>(visibility.drawArguments(1).contents);
  EXPECT_EQ(draw[0], 4u);  // vertices per instance
  EXPECT_EQ(draw[1], 3u);  // instances
}

}  // namespace
}  // namespace splatkit
