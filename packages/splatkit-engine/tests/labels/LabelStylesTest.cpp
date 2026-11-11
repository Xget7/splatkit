#include <gtest/gtest.h>
#include "splatkit/highlight/Highlight.h"
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

TEST(LabelStyles, OnlyChangedRevisionsUploadPerSlot) {
  constexpr std::size_t kWorldLabels = 45;
  splatkit::Highlight highlight(kWorldLabels);
  std::vector<uint32_t> bytes(splatkit::labelStyleBytes(kWorldLabels) / sizeof(uint32_t));
  splatkit::LabelStyleUpload slot;
  EXPECT_TRUE(slot.copy(bytes.data(), kWorldLabels, &highlight.styles()));
  EXPECT_FALSE(slot.copy(bytes.data(), kWorldLabels, &highlight.styles()));
  const uint16_t selected = 44;
  highlight.set(&selected, 1);
  EXPECT_TRUE(slot.copy(bytes.data(), kWorldLabels, &highlight.styles()));
  highlight.update(splatkit::Highlight::kFadeSeconds / 2);
  EXPECT_FALSE(slot.copy(bytes.data(), kWorldLabels, &highlight.styles()));
  const uint16_t next = 43;
  highlight.set(&next, 1);
  EXPECT_TRUE(slot.copy(bytes.data(), kWorldLabels, &highlight.styles()));
  EXPECT_EQ(slot.uploadedBytes(), kWorldLabels * sizeof(uint32_t));
  EXPECT_EQ(slot.uploadOffset(), splatkit::kPaletteBytes);
  EXPECT_EQ(highlight.styles().states.size(), kWorldLabels);
  EXPECT_EQ(bytes.size() * sizeof(uint32_t),
            splatkit::kPaletteBytes + kWorldLabels * sizeof(uint32_t));
}

TEST(LabelStyles, RejectsWrongCountsAndPaletteIndices) {
  constexpr std::size_t kWorldLabels = 45;
  splatkit::LabelStyles styles(kWorldLabels);
  EXPECT_FALSE(styles.validFor(kWorldLabels - 1));
  styles.states.back() = static_cast<uint32_t>(splatkit::kStyleStates);
  ++styles.generation;
  EXPECT_FALSE(styles.validFor(kWorldLabels));
}
