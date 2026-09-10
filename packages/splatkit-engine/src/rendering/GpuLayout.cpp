#include "splatkit/rendering/GpuLayout.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "splat/math/Half.h"

namespace splatkit {
namespace {

uint32_t packRgba8(float r, float g, float b, float a) {
  auto q = [](float v) {
    return static_cast<uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
  };
  return q(r) | (q(g) << 8) | (q(b) << 16) | (q(a) << 24);
}

uint32_t packHalf2(float a, float b) {
  return static_cast<uint32_t>(splat::toHalf(a)) | (static_cast<uint32_t>(splat::toHalf(b)) << 16);
}

}  // namespace

std::size_t shStride(int degree) {
  const auto coefficients = static_cast<std::size_t>((degree + 1) * (degree + 1) - 1);
  return (coefficients * 3 + 1) / 2;
}

bool carriesSh(const splat::SplatCloud& cloud, int degree) {
  const std::size_t n = cloud.count();
  return degree > 0 && cloud.shDegree >= degree &&
         cloud.sh.size() >=
             n * 3 * static_cast<std::size_t>((cloud.shDegree + 1) * (cloud.shDegree + 1) - 1);
}

std::vector<uint32_t> packSh(const splat::SplatCloud& cloud, int degree) {
  const std::size_t n = cloud.count();
  const std::size_t sourceCoefficients = n == 0 ? 0 : cloud.sh.size() / (n * 3);
  const auto coefficients = static_cast<std::size_t>((degree + 1) * (degree + 1) - 1);
  const std::size_t halves = coefficients * 3;
  const std::size_t stride = shStride(degree);
  std::vector<uint32_t> packed(n * stride, 0);
  for (std::size_t i = 0; i < n; ++i) {
    const float* src = &cloud.sh[i * sourceCoefficients * 3];
    for (std::size_t h = 0; h < halves; ++h) {
      const uint32_t half = splat::toHalf(src[h]);
      packed[i * stride + h / 2] |= half << ((h & 1) * 16);
    }
  }
  return packed;
}

std::vector<GpuSplat> packSplats(const splat::SplatCloud& cloud) {
  const std::size_t n = cloud.count();
  std::vector<GpuSplat> packed(n);
  for (std::size_t i = 0; i < n; ++i) {
    GpuSplat& g = packed[i];
    std::memcpy(g.position, &cloud.positions[i * 3], sizeof(g.position));
    const float alpha = cloud.alphas[i];
    g.rgba8 =
        packRgba8(cloud.colors[i * 3], cloud.colors[i * 3 + 1], cloud.colors[i * 3 + 2], alpha);
    if (alpha > 1.0f) std::memcpy(&g.lodAlpha, &alpha, sizeof(g.lodAlpha));
    const float* c = &cloud.covariances[i * 6];  // xx, xy, xz, yy, yz, zz
    g.cov[0] = packHalf2(c[0], c[1]);
    g.cov[1] = packHalf2(c[2], c[3]);
    g.cov[2] = packHalf2(c[4], c[5]);
    if (alpha <= 1.0f) g.lodAlpha = 0;
  }
  return packed;
}

}  // namespace splatkit
