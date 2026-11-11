#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>
#include "splat/formats/SplatCloud.h"
namespace splatkit {
struct LabelStyle {
  float tint[3] = {0, 0, 0};
  float tintAmount = 0;
  float brightness = 1;
  float opacity = 1;
  float reserved[2] = {0, 0};
};
static_assert(sizeof(LabelStyle) == 32);
static_assert(offsetof(LabelStyle, brightness) == 16);
inline constexpr std::size_t kLabelCount = 65536;
struct LabelStyles : std::vector<LabelStyle> {
  LabelStyles() : std::vector<LabelStyle>(kLabelCount) {}
  void fill(const LabelStyle& style) { std::fill(begin(), end(), style); }
};
// Two halfwords per word avoid requiring Vulkan 16-bit storage support.
std::vector<uint32_t> packLabels(const splat::SplatCloud& cloud);
}  // namespace splatkit
