#include "splatkit/highlight/Highlight.h"

#include <algorithm>

namespace splatkit {
namespace {

float smoothstep(float t) {
  return t * t * (3 - 2 * t);
}

LabelStyle emphasised() {
  LabelStyle style;
  std::copy(std::begin(Highlight::kTint), std::end(Highlight::kTint), style.tint);
  style.tintAmount = Highlight::kTintAmount;
  style.brightness = Highlight::kEmphasisBrightness;
  return style;
}

LabelStyle dimmed() {
  LabelStyle style;
  style.brightness = Highlight::kDimBrightness;
  return style;
}

}  // namespace

void Highlight::set(const std::uint16_t* labels, std::size_t count) {
  if (current_.states.empty()) return;
  std::vector<std::uint16_t> parts;
  for (std::size_t i = 0; labels && i < count; ++i)
    if (labels[i] < current_.states.size()) parts.push_back(labels[i]);
  std::sort(parts.begin(), parts.end());
  parts.erase(std::unique(parts.begin(), parts.end()), parts.end());
  if (parts == parts_) return;
  // Retargeting completes the previous transition before starting the next palette fade.
  parts_ = std::move(parts);
  const std::array<LabelStyle, 3> looks = {LabelStyle{}, emphasised(), dimmed()};
  for (std::size_t old = 0; old < looks.size(); ++old) {
    for (std::size_t target = 0; target < looks.size(); ++target) {
      const std::size_t state = old * looks.size() + target;
      current_.from[state] = looks[old];
      current_.to[state] = looks[target];
      // Tint fades in place rather than moving through black on the way to identity.
      const auto& tint = looks[target].tintAmount > 0 ? looks[target] : looks[old];
      std::copy_n(tint.tint, 3, current_.from[state].tint);
      std::copy_n(tint.tint, 3, current_.to[state].tint);
    }
  }
  current_.from[kHiddenState].opacity = current_.to[kHiddenState].opacity = 0;
  for (std::size_t label = 0; label < current_.states.size(); ++label) {
    const auto old = current_.states[label] % looks.size();
    const std::size_t target =
        parts_.empty() ? 0 : (std::binary_search(parts_.begin(), parts_.end(), label) ? 1 : 2);
    current_.states[label] = static_cast<uint32_t>(old * looks.size() + target);
  }
  ++current_.generation;
  current_.fade = 0;
  elapsed_ = 0;
}

bool Highlight::update(float dtSeconds) {
  if (!fading()) return false;
  elapsed_ = std::min(elapsed_ + std::max(dtSeconds, 0.0f), kFadeSeconds);
  current_.fade = smoothstep(elapsed_ / kFadeSeconds);
  return true;
}

}  // namespace splatkit
