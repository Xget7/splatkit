#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "splat/formats/SplatCloud.h"

namespace splatkit {

// The stride matches both shader ABIs so slots can copy palettes without conversion.
struct LabelStyle {
  float tint[3] = {0, 0, 0};
  float tintAmount = 0;
  float brightness = 1;
  float opacity = 1;
  float reserved[2] = {0, 0};
};
static_assert(sizeof(LabelStyle) == 32);
static_assert(offsetof(LabelStyle, brightness) == 16);
// A state occupies a whole word on Vulkan 1.1, which need not support byte storage.
// Nine previous/next appearance pairs preserve completed selections during a fade, plus hidden.
inline constexpr std::size_t kStyleStates = 10;
inline constexpr std::size_t kHiddenState = kStyleStates - 1;
enum class LabelState : uint32_t { normal = 0, highlighted = 1, dimmed = 2, hidden = 9 };
// The caller increments generation when states or palette change, but only changes fade while
// animating. The borrowed object must remain stable until draw returns.
struct LabelStyles {
  std::vector<uint32_t> states;
  std::array<LabelStyle, kStyleStates> from{};
  std::array<LabelStyle, kStyleStates> to{};
  uint64_t generation = 1;
  float fade = 1;
  explicit LabelStyles(std::size_t count = 0) : states(count, 0) {
    from[kHiddenState].opacity = to[kHiddenState].opacity = 0;
  }
  bool validFor(std::size_t count) const {
    if (states.size() != count) return false;
    if (checkedGeneration_ != generation || checkedCount_ != count) {
      checkedValid_ = states.size() == count &&
                      std::all_of(states.begin(), states.end(),
                                  [](uint32_t state) { return state < kStyleStates; });
      checkedGeneration_ = generation;
      checkedCount_ = count;
    }
    return checkedValid_;
  }
  LabelStyle operator[](std::size_t label) const;

 private:
  mutable uint64_t checkedGeneration_ = 0;
  mutable std::size_t checkedCount_ = 0;
  mutable bool checkedValid_ = false;
};
inline constexpr std::size_t kPaletteBytes = 2 * kStyleStates * sizeof(LabelStyle);
// Palette and states share one binding so the default descriptor layout stays unchanged.
inline std::size_t labelStyleBytes(std::size_t count) {
  return kPaletteBytes + count * sizeof(uint32_t);
}
// A frame slot is writable only after its fence completes. This seam makes uploads observable
// without GPU timing; fade never participates in the revision.
class LabelStyleUpload {
 public:
  bool copy(void* destination, std::size_t count, const LabelStyles* styles) {
    uploadedBytes_ = 0;
    const uint64_t generation = styles ? styles->generation : 0;
    if (valid_ && source_ == styles && generation_ == generation) return false;
    if (styles && !styles->validFor(count))
      throw std::invalid_argument("label state table must match the world and palette");
    LabelStyles defaults;
    const auto& data = styles ? *styles : defaults;
    auto* bytes = static_cast<unsigned char*>(destination);
    const bool paletteChanged =
        !valid_ || std::memcmp(bytes, data.from.data(), kPaletteBytes / 2) != 0 ||
        std::memcmp(bytes + kPaletteBytes / 2, data.to.data(), kPaletteBytes / 2) != 0;
    auto* states = reinterpret_cast<uint32_t*>(bytes + kPaletteBytes);
    bool statesChanged = !valid_;
    for (std::size_t i = 0; i < count && !statesChanged; ++i)
      statesChanged = states[i] != (styles ? data.states[i] : 0);
    if (paletteChanged) {
      std::memcpy(bytes, data.from.data(), kPaletteBytes / 2);
      std::memcpy(bytes + kPaletteBytes / 2, data.to.data(), kPaletteBytes / 2);
    }
    if (statesChanged)
      for (std::size_t i = 0; i < count; ++i) states[i] = styles ? data.states[i] : 0;
    uploadOffset_ = paletteChanged ? 0 : kPaletteBytes;
    uploadedBytes_ =
        (paletteChanged ? kPaletteBytes : 0) + (statesChanged ? count * sizeof(uint32_t) : 0);
    source_ = styles;
    generation_ = generation;
    valid_ = true;
    return uploadedBytes_ != 0;
  }
  std::size_t uploadOffset() const { return uploadOffset_; }
  std::size_t uploadedBytes() const { return uploadedBytes_; }

 private:
  std::size_t uploadOffset_ = 0;
  std::size_t uploadedBytes_ = 0;
  const LabelStyles* source_ = nullptr;
  uint64_t generation_ = 0;
  bool valid_ = false;
};
// Two halfwords per word avoid requiring Vulkan 16-bit storage support.
std::vector<uint32_t> packLabels(const splat::SplatCloud& cloud);
}  // namespace splatkit
