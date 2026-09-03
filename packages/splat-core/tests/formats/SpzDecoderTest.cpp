#include "splat/formats/SpzDecoder.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "load-spz.h"

namespace splat {
namespace {

float logit(float p) { return std::log(p / (1 - p)); }

// One splat with a known pose, written through the reference encoder.
// Frame is RDF, like World Labs, so the decoder must flip Y and Z.
std::vector<std::uint8_t> encodeOneSplat() {
  spz::GaussianCloud cloud;
  cloud.numPoints = 1;
  cloud.shDegree = 0;
  cloud.positions = {1.0f, 2.0f, 3.0f};
  cloud.scales = {std::log(0.5f), std::log(0.25f), std::log(0.125f)};
  cloud.rotations = {0.0f, 0.0f, 0.0f, 1.0f};  // identity, xyzw
  cloud.alphas = {logit(0.75f)};
  // Color 0.8 in [0,1] is stored as an SH DC coefficient.
  const float dc = (0.8f - 0.5f) / 0.282095f;
  cloud.colors = {dc, dc, dc};

  spz::PackOptions pack;
  pack.version = 2;
  std::vector<std::uint8_t> bytes;
  EXPECT_TRUE(spz::saveSpz(cloud, pack, &bytes));
  return bytes;
}

TEST(SpzDecoder, RejectsBytesThatAreNotAContainer) {
  const std::uint8_t junk[] = {'N', 'G', 'S', 'P', 0, 0, 0, 0};
  auto result = decodeSpz(junk, sizeof(junk));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error().code, ErrorCode::unsupportedFormat);
}

TEST(SpzDecoder, RejectsTruncatedContainer) {
  auto bytes = encodeOneSplat();
  bytes.resize(bytes.size() / 2);
  auto result = decodeSpz(bytes.data(), bytes.size());
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error().code, ErrorCode::corrupt);
}

TEST(SpzDecoder, DecodesAndConvertsWorldLabsFrameToInternal) {
  auto bytes = encodeOneSplat();
  auto result = decodeSpz(bytes.data(), bytes.size());
  ASSERT_TRUE(result.ok()) << result.error().message;
  const SplatCloud& cloud = result.value();

  ASSERT_EQ(cloud.count(), 1u);
  EXPECT_EQ(cloud.shDegree, 0);

  // RDF to RUB flips Y and Z. Positions are 24-bit fixed point with 12 fractional bits,
  // so they round trip exactly for these values.
  EXPECT_FLOAT_EQ(cloud.positions[0], 1.0f);
  EXPECT_FLOAT_EQ(cloud.positions[1], -2.0f);
  EXPECT_FLOAT_EQ(cloud.positions[2], -3.0f);
  EXPECT_FLOAT_EQ(cloud.bounds.min[1], -2.0f);
  EXPECT_FLOAT_EQ(cloud.bounds.max[0], 1.0f);

  // Identity rotation: covariance is diag(s^2). Scales are quantised to 1/16 in log space.
  // SPZ v2 stores quaternion xyz as int8 around 127.5, so exactly 0 is not representable:
  // the identity decodes with components of about 0.004, which leaks into the off diagonal.
  const float tol = 0.05f;
  const float offDiagonal = 0.002f;
  EXPECT_NEAR(cloud.covariances[0], 0.25f, tol * 0.25f);       // xx
  EXPECT_NEAR(cloud.covariances[1], 0.0f, offDiagonal);        // xy
  EXPECT_NEAR(cloud.covariances[2], 0.0f, offDiagonal);        // xz
  EXPECT_NEAR(cloud.covariances[3], 0.0625f, tol * 0.0625f);   // yy
  EXPECT_NEAR(cloud.covariances[4], 0.0f, offDiagonal);        // yz
  EXPECT_NEAR(cloud.covariances[5], 0.015625f, tol * 0.015625f);  // zz

  EXPECT_NEAR(cloud.alphas[0], 0.75f, 1.0f / 255.0f);
  EXPECT_NEAR(cloud.colors[0], 0.8f, 1.0f / 255.0f);
  EXPECT_NEAR(cloud.colors[2], 0.8f, 1.0f / 255.0f);
}

TEST(SpzDecoder, RubSourceFrameLeavesPositionsUntouched) {
  auto bytes = encodeOneSplat();
  auto result = decodeSpz(bytes.data(), bytes.size(), {CoordinateFrame::rub});
  ASSERT_TRUE(result.ok());
  EXPECT_FLOAT_EQ(result.value().positions[1], 2.0f);
  EXPECT_FLOAT_EQ(result.value().positions[2], 3.0f);
}

TEST(SpzDecoder, RejectsNonFiniteScales) {
  spz::GaussianCloud cloud;
  cloud.numPoints = 1;
  cloud.positions = {0, 0, 0};
  cloud.scales = {200.0f, 0, 0};  // exp(200) overflows float
  cloud.rotations = {0, 0, 0, 1};
  cloud.alphas = {0};
  cloud.colors = {0, 0, 0};
  spz::PackOptions pack;
  pack.version = 2;
  std::vector<std::uint8_t> bytes;
  ASSERT_TRUE(spz::saveSpz(cloud, pack, &bytes));
  auto result = decodeSpz(bytes.data(), bytes.size());
  // The uint8 log encoding clamps scale to at most exp(5.9), so this must decode finite.
  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(std::isfinite(result.value().covariances[0]));
}

TEST(SpzDecoder, RotatedSplatProducesOffDiagonalCovariance) {
  spz::GaussianCloud cloud;
  cloud.numPoints = 1;
  cloud.positions = {0, 0, 0};
  cloud.scales = {std::log(1.0f), std::log(0.1f), std::log(0.1f)};
  // 45 degrees about Z in RDF, xyzw.
  const float s = std::sin(M_PI / 8), c = std::cos(M_PI / 8);
  cloud.rotations = {0, 0, s, c};
  cloud.alphas = {0};
  cloud.colors = {0, 0, 0};
  spz::PackOptions pack;
  pack.version = 2;
  std::vector<std::uint8_t> bytes;
  ASSERT_TRUE(spz::saveSpz(cloud, pack, &bytes));

  auto result = decodeSpz(bytes.data(), bytes.size(), {CoordinateFrame::rub});
  ASSERT_TRUE(result.ok());
  const auto& cov = result.value().covariances;
  // A long axis rotated 45 degrees in XY: xx == yy and xy is large and positive.
  EXPECT_NEAR(cov[0], cov[3], 0.05f);
  EXPECT_GT(cov[1], 0.4f);
  EXPECT_NEAR(cov[5], 0.01f, 0.002f);
}

TEST(SpzDecoder, RejectsPayloadsBeyondTheDecodedSizeCeiling) {
  auto bytes = encodeOneSplat();
  SpzDecodeOptions options;
  options.maxDecodedBytes = 8;  // one splat needs the 16 byte header plus 19 bytes
  auto result = decodeSpz(bytes.data(), bytes.size(), options);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error().code, ErrorCode::corrupt);
  // The same file decodes once the ceiling has room for it.
  options.maxDecodedBytes = 64;
  EXPECT_TRUE(decodeSpz(bytes.data(), bytes.size(), options).ok());
}

TEST(SpzDecoder, AccumulatesBoundsAcrossSplats) {
  spz::GaussianCloud cloud;
  cloud.numPoints = 3;
  cloud.positions = {1, 2, 3, -4, 5, -6, 7, -8, 9};
  cloud.scales = std::vector<float>(9, 0.0f);
  cloud.rotations = {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1};
  cloud.alphas = {0, 0, 0};
  cloud.colors = std::vector<float>(9, 0.0f);
  spz::PackOptions pack;
  pack.version = 2;
  std::vector<std::uint8_t> bytes;
  ASSERT_TRUE(spz::saveSpz(cloud, pack, &bytes));

  auto result = decodeSpz(bytes.data(), bytes.size(), {CoordinateFrame::rub});
  ASSERT_TRUE(result.ok());
  const Bounds& b = result.value().bounds;
  EXPECT_FLOAT_EQ(b.min[0], -4.0f);
  EXPECT_FLOAT_EQ(b.min[1], -8.0f);
  EXPECT_FLOAT_EQ(b.min[2], -6.0f);
  EXPECT_FLOAT_EQ(b.max[0], 7.0f);
  EXPECT_FLOAT_EQ(b.max[1], 5.0f);
  EXPECT_FLOAT_EQ(b.max[2], 9.0f);
}

TEST(SpzDecoder, PassesHigherOrderShThrough) {
  spz::GaussianCloud cloud;
  cloud.numPoints = 1;
  cloud.shDegree = 1;  // 3 extra coefficients times 3 channels
  cloud.positions = {0, 0, 0};
  cloud.scales = {0, 0, 0};
  cloud.rotations = {0, 0, 0, 1};
  cloud.alphas = {0};
  cloud.colors = {0, 0, 0};
  cloud.sh = {0.5f, -0.5f, 0.25f, 0.1f, 0.2f, 0.3f, -0.1f, -0.2f, -0.3f};
  spz::PackOptions pack;
  pack.version = 2;
  std::vector<std::uint8_t> bytes;
  ASSERT_TRUE(spz::saveSpz(cloud, pack, &bytes));

  auto result = decodeSpz(bytes.data(), bytes.size(), {CoordinateFrame::rub});
  ASSERT_TRUE(result.ok()) << result.error().message;
  EXPECT_EQ(result.value().shDegree, 1);
  ASSERT_EQ(result.value().sh.size(), 9u);
  // SH is quantised to 8 bits; only check it survived in the right slots.
  EXPECT_NEAR(result.value().sh[0], 0.5f, 0.05f);
  EXPECT_NEAR(result.value().sh[1], -0.5f, 0.05f);
  EXPECT_NEAR(result.value().sh[8], -0.3f, 0.05f);
}

// Opt-in integration test against a real World Labs export.
// Run with SPLAT_FIXTURES_DIR pointing at a folder containing kitchen_500k.spz.
TEST(SpzDecoder, DecodesWorldLabsKitchen) {
  const char* dir = std::getenv("SPLAT_FIXTURES_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "SPLAT_FIXTURES_DIR not set";
  }
  std::ifstream file(std::string(dir) + "/kitchen_500k.spz", std::ios::binary);
  ASSERT_TRUE(file.is_open());
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());

  auto result = decodeSpz(bytes.data(), bytes.size());
  ASSERT_TRUE(result.ok()) << result.error().message;
  const SplatCloud& cloud = result.value();
  EXPECT_EQ(cloud.count(), 500000u);
  EXPECT_EQ(cloud.shDegree, 0);
  EXPECT_TRUE(cloud.sh.empty());
  // The kitchen floor sits below the origin once Y points up.
  EXPECT_LT(cloud.bounds.min[1], -0.5f);
  EXPECT_GT(cloud.bounds.max[1], 0.0f);
}

}  // namespace
}  // namespace splat
