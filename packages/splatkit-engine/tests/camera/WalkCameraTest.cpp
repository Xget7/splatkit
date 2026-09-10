#include "splatkit/camera/WalkCamera.h"

#include <cmath>

#include <gtest/gtest.h>

namespace splatkit {
namespace {

constexpr float kPi = 3.14159265358979f;

TEST(WalkCamera, StartsAtTheOriginLookingDownNegativeZ) {
  const WalkCamera camera;
  const splat::Vec3 forward = camera.rotation().transformDirection({0, 0, -1});
  EXPECT_NEAR(forward.x, 0.0f, 1e-6f);
  EXPECT_NEAR(forward.y, 0.0f, 1e-6f);
  EXPECT_NEAR(forward.z, -1.0f, 1e-6f);
  EXPECT_EQ(camera.position().x, 0.0f);
}

TEST(WalkCamera, LookAtFramesTheTargetWithTheGivenUpAndPassesThePole) {
  WalkCamera camera;
  // Straight above the origin, with +z at the top of the frame: a pose pitch cannot hold.
  camera.setLookAt({0, 10, 0}, {0, 0, 0}, {0, 0, 1});
  const splat::Vec3 forward = camera.rotation().transformDirection({0, 0, -1});
  const splat::Vec3 top = camera.rotation().transformDirection({0, 1, 0});
  EXPECT_NEAR(forward.y, -1.0f, 1e-6f);
  EXPECT_NEAR(top.z, 1.0f, 1e-6f);
  const splat::Vec3 origin = camera.viewMatrix().transformPoint({0, 0, 0});
  EXPECT_NEAR(origin.z, -10.0f, 1e-5f);
  EXPECT_NEAR(camera.pitch(), -kPi / 2, 1e-5f);
  camera.look(0.0f, 0.0f);  // a touch takes the view back to yaw and pitch
  EXPECT_NEAR(camera.rotation().transformDirection({0, 0, -1}).y, -1.0f, 1e-2f);
}

TEST(WalkCamera, YawTurnsLeftAboutUpAndWalkFollowsTheView) {
  WalkCamera camera;
  camera.look(kPi / 2, 0.0f);  // a quarter turn to the left: forward is now -x
  camera.walk(2.0f, 0.0f);
  EXPECT_NEAR(camera.position().x, -2.0f, 1e-5f);
  EXPECT_NEAR(camera.position().z, 0.0f, 1e-5f);
}

TEST(WalkCamera, PitchIsClampedAndIgnoredWhileMotionDrivesTheView) {
  WalkCamera camera;
  camera.look(0.0f, 10.0f);
  EXPECT_NEAR(camera.pitch(), 85.0f * kPi / 180.0f, 1e-5f);
  camera.setMotionEnabled(true);
  EXPECT_EQ(camera.pitch(), 0.0f);
  camera.look(0.0f, 1.0f);
  EXPECT_EQ(camera.pitch(), 0.0f);
}

TEST(WalkCamera, VelocityMovesEveryUpdate) {
  WalkCamera camera;
  camera.setVelocity(1.0f, 0.0f);
  camera.update(0.5f);
  camera.update(0.5f);
  EXPECT_NEAR(camera.position().z, -1.0f, 1e-5f);
  camera.setVelocity(0.0f, 0.0f);
  camera.update(1.0f);
  EXPECT_NEAR(camera.position().z, -1.0f, 1e-5f);
}

// A phone held upright facing north, in Android's East-North-Up frame: device x east,
// device y up, device z south (out of the screen towards the user). The camera must
// look north, which is -z in the engine's frame.
TEST(WalkCamera, AttitudeInTheReferenceFrameLooksNorth) {
  WalkCamera camera;
  const float upright[9] = {1, 0, 0, 0, 0, -1, 0, 1, 0};
  camera.setAttitude(upright);
  camera.setMotionEnabled(true);
  const splat::Vec3 forward = camera.rotation().transformDirection({0, 0, -1});
  EXPECT_NEAR(forward.x, 0.0f, 1e-5f);
  EXPECT_NEAR(forward.y, 0.0f, 1e-5f);
  EXPECT_NEAR(forward.z, -1.0f, 1e-5f);
}

}  // namespace
}  // namespace splatkit
