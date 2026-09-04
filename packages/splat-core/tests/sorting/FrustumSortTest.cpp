#include <chrono>
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
