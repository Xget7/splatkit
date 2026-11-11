#include "splatkit/labels/LabelStyles.h"
namespace splatkit {
LabelStyle LabelStyles::operator[](std::size_t label) const {
  const auto state = label < states.size() ? states[label] : 0;
  const auto& a = from[state];
  const auto& b = to[state];
  LabelStyle result;
  for (int c = 0; c < 3; ++c) result.tint[c] = a.tint[c] + (b.tint[c] - a.tint[c]) * fade;
  result.tintAmount = a.tintAmount + (b.tintAmount - a.tintAmount) * fade;
  result.brightness = a.brightness + (b.brightness - a.brightness) * fade;
  result.opacity = a.opacity + (b.opacity - a.opacity) * fade;
  return result;
}

std::vector<uint32_t> packLabels(const splat::SplatCloud& cloud) {
  if (cloud.labels.empty() || cloud.labels.size() != cloud.count()) return {};
  std::vector<uint32_t> words((cloud.count() + 1) / 2, 0);
  for (std::size_t i = 0; i < cloud.count(); ++i)
    words[i / 2] |= uint32_t{cloud.labels[i]} << ((i % 2) * 16);
  return words;
}
}  // namespace splatkit
