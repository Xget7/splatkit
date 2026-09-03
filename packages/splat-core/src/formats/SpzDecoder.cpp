#include "splat/formats/SpzDecoder.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include "load-spz.h"

// spz's unpack applies convertCoordinates(RUB, options.to) unconditionally in this build.
// With extensions enabled it would instead read a per-file tag and convert for real, and
// the explicit conversion below would then flip twice. Revisit this file before enabling.
#ifdef SPZ_BUILD_EXTENSIONS
#error "SpzDecoder assumes spz without extensions; see the frame conversion below"
#endif

namespace splat {
namespace {

constexpr float kShC0 = 0.282095f;

bool looksLikeGzip(const std::uint8_t* data, std::size_t size) {
  return size >= 2 && data[0] == 0x1f && data[1] == 0x8b;
}

bool looksLikeZstd(const std::uint8_t* data, std::size_t size) {
  return size >= 4 && data[0] == 0x28 && data[1] == 0xb5 && data[2] == 0x2f && data[3] == 0xfd;
}

// Decompressed size the container declares, or nullopt when it does not say.
// gzip stores it in the last four bytes (ISIZE, little endian, modulo 2^32). A payload
// beyond 4 GB would wrap, but deflate cannot exceed a ratio of about 1032:1, so a wrapped
// value still needs a compressed input larger than anything this library would accept.
// zstd may omit the size from the frame header; that case is left to the caller's ceiling
// on the compressed input.
std::optional<std::uint64_t> declaredDecodedSize(const std::uint8_t* data, std::size_t size) {
  if (looksLikeGzip(data, size) && size >= 18) {
    const std::uint8_t* t = data + size - 4;
    return std::uint64_t(t[0]) | (std::uint64_t(t[1]) << 8) | (std::uint64_t(t[2]) << 16) |
           (std::uint64_t(t[3]) << 24);
  }
  return std::nullopt;
}

spz::CoordinateSystem toSpz(CoordinateFrame frame) {
  switch (frame) {
    case CoordinateFrame::rdf:
      return spz::CoordinateSystem::RDF;
    case CoordinateFrame::rub:
      return spz::CoordinateSystem::RUB;
  }
  return spz::CoordinateSystem::UNSPECIFIED;
}

// Sigma = R * diag(s)^2 * R^T for a unit quaternion (x, y, z, w) and scales s.
// Returns the upper triangle xx, xy, xz, yy, yz, zz.
std::array<float, 6> covariance(const float* quaternion, const float* scale) {
  const float x = quaternion[0];
  const float y = quaternion[1];
  const float z = quaternion[2];
  const float w = quaternion[3];

  // Rotation matrix, rXY = row X, column Y.
  const float r00 = 1 - 2 * (y * y + z * z);
  const float r01 = 2 * (x * y - w * z);
  const float r02 = 2 * (x * z + w * y);
  const float r10 = 2 * (x * y + w * z);
  const float r11 = 1 - 2 * (x * x + z * z);
  const float r12 = 2 * (y * z - w * x);
  const float r20 = 2 * (x * z - w * y);
  const float r21 = 2 * (y * z + w * x);
  const float r22 = 1 - 2 * (x * x + y * y);

  // M = R * S, so Sigma = M * M^T.
  const float m00 = r00 * scale[0], m01 = r01 * scale[1], m02 = r02 * scale[2];
  const float m10 = r10 * scale[0], m11 = r11 * scale[1], m12 = r12 * scale[2];
  const float m20 = r20 * scale[0], m21 = r21 * scale[1], m22 = r22 * scale[2];

  return {
      m00 * m00 + m01 * m01 + m02 * m02,  // xx
      m00 * m10 + m01 * m11 + m02 * m12,  // xy
      m00 * m20 + m01 * m21 + m02 * m22,  // xz
      m10 * m10 + m11 * m11 + m12 * m12,  // yy
      m10 * m20 + m11 * m21 + m12 * m22,  // yz
      m20 * m20 + m21 * m21 + m22 * m22,  // zz
  };
}

}  // namespace

Result<SplatCloud> decodeSpz(const std::uint8_t* data, std::size_t size,
                             const SpzDecodeOptions& options) {
  if (!looksLikeGzip(data, size) && !looksLikeZstd(data, size)) {
    return Error{ErrorCode::unsupportedFormat, "not an SPZ container (expected gzip or zstd)"};
  }

  if (const auto declared = declaredDecodedSize(data, size);
      declared && *declared > options.maxDecodedBytes) {
    return Error{ErrorCode::corrupt, "SPZ payload exceeds the decoded size ceiling"};
  }

  spz::UnpackOptions unpack;
  unpack.to = toSpz(kInternalFrame);
  spz::GaussianCloud cloud = spz::loadSpz(data, size, unpack);
  if (cloud.numPoints <= 0) {
    return Error{ErrorCode::corrupt, "SPZ container could not be decoded"};
  }
  // spz does not read a frame tag in this build: unpack ran convertCoordinates(RUB, RUB),
  // an identity. World Labs files carry no tag, so this is the one real conversion.
  cloud.convertCoordinates(toSpz(options.sourceFrame), toSpz(kInternalFrame));

  const auto n = static_cast<std::size_t>(cloud.numPoints);
  const bool sizesMatch = cloud.positions.size() == n * 3 && cloud.scales.size() == n * 3 &&
                          cloud.rotations.size() == n * 4 && cloud.alphas.size() == n &&
                          cloud.colors.size() == n * 3;
  if (!sizesMatch) {
    return Error{ErrorCode::corrupt, "SPZ attribute arrays do not match the point count"};
  }

  SplatCloud out;
  out.positions = std::move(cloud.positions);
  out.covariances.resize(n * 6);
  out.colors.resize(n * 3);
  out.alphas.resize(n);
  out.shDegree = cloud.shDegree;
  out.sh = std::move(cloud.sh);

  constexpr float inf = std::numeric_limits<float>::infinity();
  out.bounds.min = {inf, inf, inf};
  out.bounds.max = {-inf, -inf, -inf};

  bool finite = true;
  for (std::size_t i = 0; i < n; ++i) {
    float scale[3];
    for (int k = 0; k < 3; ++k) {
      scale[k] = std::exp(cloud.scales[i * 3 + k]);
      finite = finite && std::isfinite(out.positions[i * 3 + k]) && std::isfinite(scale[k]);
      const float c = std::clamp(0.5f + kShC0 * cloud.colors[i * 3 + k], 0.0f, 1.0f);
      out.colors[i * 3 + k] = c;
      const float p = out.positions[i * 3 + k];
      out.bounds.min[k] = std::min(out.bounds.min[k], p);
      out.bounds.max[k] = std::max(out.bounds.max[k], p);
    }
    const auto cov = covariance(&cloud.rotations[i * 4], scale);
    std::copy(cov.begin(), cov.end(), out.covariances.begin() + static_cast<std::ptrdiff_t>(i * 6));
    out.alphas[i] = 1.0f / (1.0f + std::exp(-cloud.alphas[i]));
  }
  // A NaN position would sort to the front and a NaN covariance would draw garbage.
  if (!finite) return Error{ErrorCode::corrupt, "SPZ contains non-finite positions or scales"};

  return out;
}

}  // namespace splat
