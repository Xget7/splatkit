#include "splat/navigation/CharacterController.h"

#include <algorithm>
#include <cmath>

namespace splat {

CharacterController::CharacterController(const Collider& collider, CharacterSettings settings)
    : collider_(collider), settings_(settings) {}

std::optional<float> CharacterController::floorBelow(Vec3 at) const {
  const Vec3 feet = at - Vec3{0, settings_.eyeHeight, 0};
  const Vec3 from = feet + Vec3{0, settings_.floorProbeUp, 0};
  const auto hit = collider_.raycast(from, {0, -1, 0}, settings_.floorProbeUp + settings_.floorProbeDown);
  if (!hit) return std::nullopt;
  return hit->point.y;
}

bool CharacterController::move(Vec3 delta) {
  Vec3 d{delta.x, 0, delta.z};
  const float len = length(d);
  if (len < 1e-5f) return false;
  const Vec3 dir = d / len;

  // Probe at hip height so low furniture blocks too; slide along whatever we hit.
  const Vec3 probe = position_ - Vec3{0, settings_.hipHeight, 0};
  // The ray may reach the wall at a grazing angle, so bodyRadius is measured along the
  // wall normal and converted to ray distance: radius / cos(angle to the normal).
  if (const auto hit = collider_.raycast(probe, dir, len + settings_.bodyRadius * 10)) {
    const float cosine = std::max(0.1f, -dot(hit->normal, dir));
    const float allowed = std::clamp(hit->distance - settings_.bodyRadius / cosine, 0.0f, len);
    const Vec3 blocked = d - dir * allowed;
    Vec3 n = hit->normal;
    n.y = 0;
    Vec3 slide{};
    if (length(n) > 1e-4f) {
      n = normalize(n);
      slide = blocked - n * dot(blocked, n);
    }
    d = dir * allowed;
    if (length(slide) > 1e-5f &&
        !collider_.raycast(probe + d, normalize(slide), length(slide) + settings_.bodyRadius)) {
      d += slide;
    }
  }
  if (length(d) < 1e-5f) return false;

  // The collider is also the boundary of the generated world: refuse steps with no floor.
  const Vec3 next = position_ + d;
  if (!floorBelow(next)) return false;
  position_ = next;
  return true;
}

void CharacterController::update(float dtSeconds) {
  if (const auto floor = floorBelow(position_)) {
    const float target = *floor + settings_.eyeHeight;
    position_.y += (target - position_.y) * std::min(1.0f, dtSeconds * settings_.snapRate);
  }
}

}  // namespace splat
