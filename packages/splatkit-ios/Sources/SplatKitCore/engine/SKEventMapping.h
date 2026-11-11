#pragma once
#include <atomic>
#include <optional>
#include "SplatKit/SKSplatEngine.h"
#include "splatkit/engine/SplatEngine.h"
#include "splatkit/Log.h"
namespace splatkit {
inline std::optional<SKSplatEvent> publicEvent(SplatEngine::Event event) {
  switch (event) {
    case SplatEngine::Event::worldReady: return SKSplatEventWorldReady;
    case SplatEngine::Event::worldFailed: return SKSplatEventWorldFailed;
    case SplatEngine::Event::colliderReady: return SKSplatEventColliderReady;
    case SplatEngine::Event::colliderFailed: return SKSplatEventColliderFailed;
    case SplatEngine::Event::gpuFailed: return SKSplatEventGpuFailed;
    // The adapter emits readiness only after GPU completion.
    case SplatEngine::Event::worldFrameReady: return std::nullopt;
  }
  static std::atomic<bool> logged{false};
  if (!logged.exchange(true)) LOGE("Unknown engine event dropped");
  return std::nullopt;
}
}
