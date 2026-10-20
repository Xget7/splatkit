#pragma once

#include <cstdint>

namespace splatkit {

// Frames the CPU records while the GPU still draws earlier ones. Every per-frame slot array
// and descriptor pool in the Vulkan renderer is sized from this one count.
inline constexpr uint32_t kFramesInFlight = 2;

}  // namespace splatkit
