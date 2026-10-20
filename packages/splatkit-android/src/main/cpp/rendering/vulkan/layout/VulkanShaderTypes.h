#pragma once

#include <cstddef>
#include <cstdint>

#include "splat/math/Mat4.h"

namespace splatkit {

// Host ABI shared by splat.vert and visibility.comp, independent of either pass.
// Source splat records are the platform-independent GpuSplat in GpuLayout.h.
struct alignas(16) CameraUniform {
  splat::Mat4 view;
  splat::Mat4 proj;
  float focal[2];
  float tanHalfFov[2];
  float screenSize[2];
  uint32_t outputLinear;
  // Clockwise, 0 to 3; only splat.vert reads it (VulkanSwapchain::quarterTurns).
  uint32_t quarterTurns;
  float cameraPosition[4];
};
static_assert(sizeof(CameraUniform) == 176);
static_assert(offsetof(CameraUniform, proj) == 64);
static_assert(offsetof(CameraUniform, focal) == 128);
static_assert(offsetof(CameraUniform, tanHalfFov) == 136);
static_assert(offsetof(CameraUniform, screenSize) == 144);
static_assert(offsetof(CameraUniform, outputLinear) == 152);
static_assert(offsetof(CameraUniform, quarterTurns) == 156);
static_assert(offsetof(CameraUniform, cameraPosition) == 160);

}  // namespace splatkit
