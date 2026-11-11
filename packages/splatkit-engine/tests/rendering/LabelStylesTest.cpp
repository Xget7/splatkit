#include <gtest/gtest.h>
#include "splatkit/labels/LabelStyles.h"
#include "splatkit/rendering/GpuLayout.h"
namespace splatkit {
TEST(LabelStyles, PacksWideLabelsWithoutChangingExistingRecords) {
  EXPECT_EQ(sizeof(GpuSplat), 32u);
  EXPECT_EQ(sizeof(ProjectedSplat), 32u);
  EXPECT_EQ(sizeof(LabelStyle), 32u);
  EXPECT_EQ(offsetof(LabelStyle, opacity), 20u);
  splat::SplatCloud cloud;
  cloud.positions.resize(9);
  cloud.alphas = {1, 1, 1};
  EXPECT_TRUE(packLabels(cloud).empty());
  cloud.labels = {256, 65535, 42};
  EXPECT_EQ(packLabels(cloud), (std::vector<uint32_t>{0xffff0100u, 42u}));
  cloud.labels.pop_back();
  EXPECT_TRUE(packLabels(cloud).empty());
}
}  // namespace splatkit
