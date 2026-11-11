#include <gtest/gtest.h>
#include "load-spz.h"
#include "splatkit/engine/SplatEngine.h"
#include "support/StubRenderer.h"
namespace splatkit {
TEST(Reveal, RisesAndRestoresIdle) {
  Reveal sweep;
  splat::Bounds bounds{{0, 0, 0}, {1, 10, 1}};
  RevealSettings settings;
  settings.seconds = 1;
  sweep.start(bounds, settings);
  EXPECT_LT(sweep.level(), 0);
  EXPECT_FLOAT_EQ(sweep.band(), 0.8f);
  float previous = sweep.level();
  for (int i = 0; i < 9; ++i) {
    EXPECT_TRUE(sweep.update(0.1f));
    EXPECT_GT(sweep.level(), previous);
    previous = sweep.level();
  }
  EXPECT_FALSE(sweep.update(-1));
  EXPECT_FALSE(sweep.update(std::numeric_limits<float>::quiet_NaN()));
  sweep.update(1);
  EXPECT_EQ(sweep.level(), std::numeric_limits<float>::infinity());
  EXPECT_EQ(sweep.band(), 0);
  EXPECT_FALSE(sweep.update(1));
  settings.seconds = 0;
  sweep.start(bounds, settings);
  EXPECT_FALSE(sweep.active());
  settings.seconds = 1;
  bounds.max[1] = 0;
  sweep.start(bounds, settings);
  EXPECT_FALSE(sweep.active());
}
TEST(Reveal, EngineAppliesSettingsOnLoadAndRedrawsUntilIdle) {
  class RecordingRenderer : public StubRenderer {
   public:
    RevealUniform reveal;
    bool accept = true;
    bool draw(const Frame& frame) override {
      reveal = frame.reveal;
      return accept && StubRenderer::draw(frame);
    }
  };
  spz::GaussianCloud cloud;
  cloud.numPoints = 2;
  cloud.positions = {0, 0, -2, 0, 10, -2};
  cloud.scales = {0, 0, 0, 0, 0, 0};
  cloud.rotations = {0, 0, 0, 1, 0, 0, 0, 1};
  cloud.alphas = {0, 0};
  cloud.colors = {0, 0, 0, 0, 0, 0};
  std::vector<uint8_t> bytes;
  ASSERT_TRUE(spz::saveSpz(cloud, spz::PackOptions{}, &bytes));
  for (const bool enabled : {false, true}) {
    auto renderer = std::make_unique<RecordingRenderer>();
    auto* observed = renderer.get();
    SplatEngine engine(std::move(renderer));
    engine.setSplatBudget(0);
    if (enabled) {
      RevealSettings settings;
      settings.seconds = 1;
      ASSERT_TRUE(engine.setReveal(settings));
    }
    engine.loadWorld(bytes.data(), bytes.size());
    observed->accept = false;
    EXPECT_FALSE(engine.render(1));
    const float waiting = observed->reveal.level;
    EXPECT_FALSE(engine.render(10'000'000'001));
    EXPECT_EQ(observed->reveal.level, waiting);
    observed->accept = true;
    ASSERT_TRUE(engine.render(20'000'000'001));
    EXPECT_EQ(observed->reveal.level, waiting);
    const float first = observed->reveal.level;
    engine.render(20'500'000'001);
    if (enabled)
      EXPECT_GT(observed->reveal.level, first);
    else
      EXPECT_EQ(first, std::numeric_limits<float>::infinity());
    engine.render(120'500'000'001);
    EXPECT_EQ(observed->reveal.level, std::numeric_limits<float>::infinity());
    EXPECT_EQ(observed->reveal.band, 0);
    const auto frames = observed->frames;
    engine.render(31'000'000'001);
    EXPECT_EQ(observed->frames, frames);
  }
}
}  // namespace splatkit

TEST(Reveal, RejectsEveryOutOfRangeSetting) {
  splatkit::SplatEngine engine(std::make_unique<splatkit::StubRenderer>());
  using Settings = splatkit::RevealSettings;
  const float maxima[] = {Settings::kMaxSeconds, Settings::kMaxTintAmount,
                         Settings::kMaxBrightnessBoost, Settings::kMaxBandShare,
                         Settings::kMaxTint, Settings::kMaxTint, Settings::kMaxTint};
  for (int i = 0; i < 7; ++i) {
    Settings settings;
    float* fields[] = {&settings.seconds, &settings.tintAmount, &settings.brightnessBoost,
                      &settings.bandShare, &settings.tint[0], &settings.tint[1], &settings.tint[2]};
    for (const float value : {0.0f, maxima[i]}) {
      *fields[i] = value;
      EXPECT_TRUE(engine.setReveal(settings));
    }
    for (const float value : {-0.01f, maxima[i] + 0.01f,
                              std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()}) {
      *fields[i] = value;
      EXPECT_FALSE(engine.setReveal(settings));
    }
  }
}
