#pragma once

#include <memory>

#include <android/native_window.h>

#include "rendering/vulkan/FrameLoop.h"
#include "rendering/vulkan/VulkanContext.h"
#include "rendering/vulkan/VulkanSplatRenderer.h"
#include "splat/core/Result.h"
#include "splatkit/engine/SplatEngine.h"

namespace splatkit {

// The engine on Android: the Vulkan context and frame loop the renderer needs, the
// renderer, and the shared engine over it. What the JNI holds.
class AndroidEngine {
 public:
  static splat::Result<std::unique_ptr<AndroidEngine>> create();
  ~AndroidEngine();

  AndroidEngine(const AndroidEngine&) = delete;
  AndroidEngine& operator=(const AndroidEngine&) = delete;

  SplatEngine& engine() { return *engine_; }
  // A new window (takes a reference) or nullptr when the surface is going away.
  void setWindow(ANativeWindow* window) { renderer_->setWindow(window); }
  // The window changed size while staying attached. Rebuilds the swapchain if needed.
  void onSurfaceResized(uint32_t width, uint32_t height) {
    renderer_->onSurfaceResized(width, height);
  }

 private:
  AndroidEngine() = default;

  std::unique_ptr<VulkanContext> ctx_;
  std::unique_ptr<FrameLoop> frameLoop_;
  VulkanSplatRenderer* renderer_ = nullptr;  // owned by the engine
  std::unique_ptr<SplatEngine> engine_;      // last: dies first, with the renderer
};

}  // namespace splatkit
