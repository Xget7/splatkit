#include "splatkit/labels/LabelStyles.h"

#include <stdexcept>

#include <gtest/gtest.h>

TEST(LabelStyles, RejectsWrongCountsAndPaletteIndices) {
  constexpr std::size_t kWorldLabels = 45;
  splatkit::LabelStyles styles(kWorldLabels);
  EXPECT_FALSE(styles.validFor(kWorldLabels - 1));
  auto states = styles.states();
  states.back() = splatkit::kStyleStates;
  EXPECT_THROW(styles.setStates(states), std::invalid_argument);
}
