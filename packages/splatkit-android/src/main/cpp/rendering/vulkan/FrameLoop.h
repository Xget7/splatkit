#pragma once

#include <array>
#include <vector>

#include <vulkan/vulkan.h>

#include "rendering/vulkan/Swapchain.h"
#include "rendering/vulkan/VulkanContext.h"

namespace splatkit {

// The per-frame synchronisation: command buffers, fences and semaphores.
//
// Two frames in flight: while the GPU draws frame N, the CPU records frame N+1.
// Per frame: a command pool (reset wholesale, cheaper than resetting buffers), the
// fence the CPU waits on before reusing it, and the semaphore signalled when the
// swapchain image is ready. Per swapchain image: the semaphore the present waits on,
// because presentation may still be reading it when the frame slot comes around again.
class FrameLoop {
 public:
  static constexpr uint32_t kFramesInFlight = 2;

  // `swapchainSuboptimal`: the frame was presented, but the surface reports the swapchain
  // no longer matches it. The owner decides whether that needs a rebuild.
  enum class Status { ok, swapchainSuboptimal, swapchainOutOfDate, error };

  explicit FrameLoop(const VulkanContext& ctx);
  ~FrameLoop();

  FrameLoop(const FrameLoop&) = delete;
  FrameLoop& operator=(const FrameLoop&) = delete;

  bool valid() const { return valid_; }
  // Slot of the frame being recorded; valid between beginFrame and endFrame.
  uint32_t currentSlot() const { return current_; }

  // Call after a swapchain is created or recreated.
  bool onSwapchainCreated(const Swapchain& swapchain);

  // Waits for this frame slot, acquires an image and begins recording.
  Status beginFrame(const Swapchain& swapchain, uint32_t& imageIndex, VkCommandBuffer& cmd);
  // Ends recording, submits and presents.
  Status endFrame(const Swapchain& swapchain, uint32_t imageIndex);

 private:
  struct Frame {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence inFlight = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
  };

  void destroyRenderFinished();

  const VulkanContext& ctx_;
  std::array<Frame, kFramesInFlight> frames_{};
  std::vector<VkSemaphore> renderFinished_;
  uint32_t current_ = 0;
  bool valid_ = false;
};

}  // namespace splatkit
