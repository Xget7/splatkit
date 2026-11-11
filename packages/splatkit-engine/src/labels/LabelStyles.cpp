#include "splatkit/labels/LabelStyles.h"
namespace splatkit {
std::vector<uint32_t> packLabels(const splat::SplatCloud& cloud) {
  if (cloud.labels.empty() || cloud.labels.size() != cloud.count()) return {};
  std::vector<uint32_t> words((cloud.count() + 1) / 2, 0);
  for (std::size_t i = 0; i < cloud.count(); ++i)
    words[i / 2] |= uint32_t{cloud.labels[i]} << ((i % 2) * 16);
  return words;
}
}  // namespace splatkit
