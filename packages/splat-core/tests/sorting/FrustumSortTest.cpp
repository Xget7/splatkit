#include <algorithm>
#include <chrono>
#include <random>
#include <thread>

#include <gtest/gtest.h>

#include "splat/math/Frustum.h"
#include "splat/sorting/AsyncSorter.h"
#include "splat/sorting/DistanceSorter.h"

using splat::Frustum;
using splat::Vec3;

namespace {

// Camera at the origin looking down -Z with a 90 degree field of view.
Frustum lookingForward(float margin = 1.0f) {
  return Frustum::make({0, 0, 0}, {0, 0, -1}, {0, 1, 0}, 1.0f, 1.0f, margin);
}

}  // namespace

TEST(Frustum, ContainsPointsInFrontInsideTheFieldOfView) {
  const Frustum f = lookingForward();
  EXPECT_TRUE(f.contains({0, 0, -1}));
  EXPECT_TRUE(f.contains({0.9f, 0.9f, -1}));
  EXPECT_FALSE(f.contains({1.1f, 0, -1}));
  EXPECT_FALSE(f.contains({0, -1.1f, -1}));
  EXPECT_FALSE(f.contains({0, 0, 1}));   // behind
  EXPECT_FALSE(f.contains({0, 0, 0}));   // at the eye
}

TEST(Frustum, MarginWidensTheVolume) {
  EXPECT_FALSE(lookingForward(1.0f).contains({1.3f, 0, -1}));
  EXPECT_TRUE(lookingForward(1.5f).contains({1.3f, 0, -1}));
}

TEST(DistanceSorter, SortVisibleKeepsOnlyTheFrustumBackToFront) {
  // Two in view at different depths, one behind, one far to the side.
  splat::DistanceSorter sorter({0, 0, -1, 0, 0, -5, 0, 0, 3, 9, 0, -1});
  std::vector<uint32_t> order;
  const std::size_t visible = sorter.sortVisible(lookingForward(), order);
  ASSERT_EQ(visible, 2u);
  ASSERT_EQ(order.size(), 2u);
  EXPECT_EQ(order[0], 1u);  // farthest first
  EXPECT_EQ(order[1], 0u);
}

TEST(AsyncSorter, FrustumRequestsDeliverOnlyVisibleSplats) {
  splat::AsyncSorter sorter({0, 0, -1, 0, 0, 3});
  sorter.requestVisible(lookingForward());
  std::optional<splat::AsyncSorter::Result> result;
  for (int i = 0; i < 1000 && !result; ++i) {
    result = sorter.take();
    if (!result) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->order.size(), 1u);
  EXPECT_EQ(result->order[0], 0u);
}

// Large enough for the parallel cull path: every visible splat must be kept exactly once
// and the order must still be back to front.
TEST(DistanceSorter, SortVisibleMatchesTheSequentialAnswerOnLargeClouds) {
  std::mt19937 rng(21);
  std::uniform_real_distribution<float> u(-20.0f, 20.0f);
  const std::size_t n = 450000;
  std::vector<float> positions(n * 3);
  for (float& v : positions) v = u(rng);
  const Frustum f = lookingForward(1.2f);

  std::vector<uint32_t> expected;
  std::vector<float> expectedDistances;
  for (std::size_t i = 0; i < n; ++i) {
    const Vec3 p{positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]};
    if (f.contains(p)) expected.push_back(static_cast<uint32_t>(i));
  }

  splat::DistanceSorter sorter(positions);
  std::vector<uint32_t> order;
  ASSERT_EQ(sorter.sortVisible(f, order), expected.size());
  std::vector<uint32_t> sortedIndices = order;
  std::sort(sortedIndices.begin(), sortedIndices.end());
  EXPECT_EQ(sortedIndices, expected);
  for (std::size_t k = 1; k < order.size(); ++k) {
    const float* a = &positions[order[k - 1] * 3];
    const float* b = &positions[order[k] * 3];
    const float da = a[0] * a[0] + a[1] * a[1] + a[2] * a[2];
    const float db = b[0] * b[0] + b[1] * b[1] + b[2] * b[2];
    ASSERT_GE(da, db) << "not back to front at " << k;
  }
}
