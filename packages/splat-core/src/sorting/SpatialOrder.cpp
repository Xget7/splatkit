#include "splat/sorting/SpatialOrder.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>

namespace splat {
namespace {

// Spreads the low 10 bits of v so that each bit lands in every third position.
std::uint32_t spreadBits(std::uint32_t v) {
  v = (v | (v << 16)) & 0x030000ffu;
  v = (v | (v << 8)) & 0x0300f00fu;
  v = (v | (v << 4)) & 0x030c30c3u;
  v = (v | (v << 2)) & 0x09249249u;
  return v;
}

std::uint32_t quantise(float value, float min, float max) {
  const float extent = max - min;
  if (!(extent > 0.0f)) return 0;
  const float t = (value - min) / extent;
  const float scaled = std::floor(t * 1024.0f);
  return static_cast<std::uint32_t>(std::clamp(scaled, 0.0f, 1023.0f));
}

template <typename T>
void permute(std::vector<T>& values, std::size_t stride, const std::vector<std::uint32_t>& order) {
  if (values.empty()) return;
  std::vector<T> out(values.size());
  for (std::size_t i = 0; i < order.size(); ++i) {
    const T* src = &values[static_cast<std::size_t>(order[i]) * stride];
    std::copy(src, src + stride, &out[i * stride]);
  }
  values.swap(out);
}

}  // namespace

std::uint32_t mortonCode(const float* p, const Bounds& b) {
  const std::uint32_t x = quantise(p[0], b.min[0], b.max[0]);
  const std::uint32_t y = quantise(p[1], b.min[1], b.max[1]);
  const std::uint32_t z = quantise(p[2], b.min[2], b.max[2]);
  return spreadBits(x) | (spreadBits(y) << 1) | (spreadBits(z) << 2);
}

void reorderSpatially(SplatCloud& cloud) {
  const std::size_t n = cloud.count();
  if (n < 2) return;
  std::vector<std::uint64_t> keys(n);  // code in the high bits, index below: a stable order
  for (std::size_t i = 0; i < n; ++i) {
    keys[i] = (static_cast<std::uint64_t>(mortonCode(&cloud.positions[i * 3], cloud.bounds)) << 32) | i;
  }
  std::sort(keys.begin(), keys.end());
  std::vector<std::uint32_t> order(n);
  for (std::size_t i = 0; i < n; ++i) order[i] = static_cast<std::uint32_t>(keys[i] & 0xffffffffu);

  permute(cloud.positions, 3, order);
  permute(cloud.covariances, 6, order);
  permute(cloud.colors, 3, order);
  permute(cloud.alphas, 1, order);
  if (!cloud.sh.empty()) permute(cloud.sh, cloud.sh.size() / n, order);
}

}  // namespace splat
