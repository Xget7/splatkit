#include "camera/WalkCamera.h"

#include <algorithm>
#include <cmath>

namespace splatkit {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kMaxPitch = 85.0f * kPi / 180.0f;

}  // namespace

WalkCamera::WalkCamera()
    // Android's reference frame is East-North-Up (Z up). Rotating -90 degrees about X
    // sends Z to Y (up) and north to -Z (forward), the frame every renderer here uses.
    : referenceToWorld_(splat::Mat4::rotation(-kPi / 2, {1, 0, 0})) {}

void WalkCamera::setCollider(std::unique_ptr<splat::Collider> collider) {
  const splat::Vec3 current = position();
  collider_ = std::move(collider);
  player_.reset();
  if (collider_) {
    player_ = std::make_unique<splat::CharacterController>(*collider_);
    player_->setPosition(current);
  } else {
    freePosition_ = current;
  }
}

void WalkCamera::look(float deltaYaw, float deltaPitch) {
  yaw_ += deltaYaw;
  if (!motion_) pitch_ = std::clamp(pitch_ + deltaPitch, -kMaxPitch, kMaxPitch);
}

void WalkCamera::walk(float forward, float right) {
  const splat::Mat4 r = rotation();
  const splat::Vec3 fwd = r.transformDirection({0, 0, -1});
  const splat::Vec3 rgt = r.transformDirection({1, 0, 0});
  const splat::Vec3 delta = fwd * forward + rgt * right;
  if (player_) {
    player_->move(delta);  // flattens to the floor plane and collides
  } else {
    freePosition_ += delta;
  }
}

void WalkCamera::setVelocity(float forward, float right) {
  velocityForward_ = forward;
  velocityRight_ = right;
}

void WalkCamera::setAttitude(const float rowMajor[9]) {
  splat::Mat4 m = splat::Mat4::identity();
  for (int row = 0; row < 3; ++row)
    for (int col = 0; col < 3; ++col) m.at(row, col) = rowMajor[row * 3 + col];
  attitude_ = m;
}

void WalkCamera::setMotionEnabled(bool enabled) {
  motion_ = enabled;
  if (enabled) pitch_ = 0;
}

void WalkCamera::update(float dtSeconds) {
  if (velocityForward_ != 0 || velocityRight_ != 0) {
    walk(velocityForward_ * dtSeconds, velocityRight_ * dtSeconds);
  }
  if (player_) player_->update(dtSeconds);
}

splat::Vec3 WalkCamera::position() const { return player_ ? player_->position() : freePosition_; }

splat::Mat4 WalkCamera::rotation() const {
  const splat::Mat4 yaw = splat::Mat4::rotation(yaw_, {0, 1, 0});
  if (motion_) return yaw * referenceToWorld_ * attitude_;
  return yaw * splat::Mat4::rotation(pitch_, {1, 0, 0});
}

splat::Mat4 WalkCamera::viewMatrix() const {
  return (splat::Mat4::translation(position()) * rotation()).rigidInverse();
}

}  // namespace splatkit
