#pragma once

#include "splat/math/Vec3.h"

namespace splat {

// A camera's view volume for point tests: origin, orthonormal axes and the half extents
// per unit of depth. `margin` widens the volume so that content entering the view during
// a turn is already sorted in; 1.0 is the exact field of view.
struct Frustum {
  Vec3 origin;
  Vec3 forward;
  Vec3 right;
  Vec3 up;
  float tanHalfX = 1.0f;
  float tanHalfY = 1.0f;

  static Frustum make(Vec3 origin, Vec3 forward, Vec3 up, float tanHalfX, float tanHalfY, float margin) {
    Frustum f;
    f.origin = origin;
    f.forward = normalize(forward);
    f.right = normalize(cross(f.forward, up));
    f.up = cross(f.right, f.forward);
    f.tanHalfX = tanHalfX * margin;
    f.tanHalfY = tanHalfY * margin;
    return f;
  }

  // True when the point is in front of the camera and inside the widened field of view.
  bool contains(Vec3 p) const {
    const Vec3 d = p - origin;
    const float z = dot(d, forward);
    if (z <= 0.0f) return false;
    const float x = dot(d, right);
    if (x > z * tanHalfX || x < -z * tanHalfX) return false;
    const float y = dot(d, up);
    return y <= z * tanHalfY && y >= -z * tanHalfY;
  }
};

}  // namespace splat
