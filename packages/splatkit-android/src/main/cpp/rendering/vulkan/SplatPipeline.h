#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include <vulkan/vulkan.h>

#include "rendering/vulkan/FrameLoop.h"
#include "rendering/vulkan/GpuBuffer.h"
#include "rendering/vulkan/VulkanContext.h"
#include "rendering/vulkan/VulkanShaderTypes.h"
#include "splat/core/Result.h"
#include "splat/formats/SplatCloud.h"
#include "splat/math/Mat4.h"
#include "splat/math/Vec3.h"
#include "splatkit/rendering/GpuLayout.h"

namespace splatkit {

// The world on the GPU: splats plus the draw order the sorter writes.
struct GpuWorld {
  std::unique_ptr<GpuBuffer> splats;
  std::unique_ptr<GpuBuffer> order;
  // Spherical harmonics bands 1 to `shDegree`, rgb halves per coefficient, packed two per
  // uint with no padding. A placeholder of one uint when the degree is 0, so the
  // descriptor is always valid and the degree 0 pipeline never reads it.
  std::unique_ptr<GpuBuffer> sh;
  int shDegree = 0;
  // One host visible staging buffer per frame slot: the slot's fence guarantees the GPU
  // finished reading it before the CPU writes the next order into it.
  std::array<std::unique_ptr<GpuBuffer>, FrameLoop::kFramesInFlight> orderStaging;
  uint32_t count = 0;
};

// The Gaussian splat pipeline: one instanced draw, four vertices per splat, blended
// back to front with no depth test. Owns the descriptor set layout, the pipeline and
// one camera uniform per frame in flight.
class SplatPipeline {
 public:
  static splat::Result<std::unique_ptr<SplatPipeline>> create(const VulkanContext& ctx,
                                                              VkRenderPass renderPass,
                                                              bool swapchainIsSrgb);
  ~SplatPipeline();

  SplatPipeline(const SplatPipeline&) = delete;
  SplatPipeline& operator=(const SplatPipeline&) = delete;

  // Converts a decoded cloud to the GPU layout and uploads it (blocking). Spherical
  // harmonics above `maxShDegree` are dropped: degree 3 costs 92 bytes per splat.
  std::unique_ptr<GpuWorld> uploadWorld(const splat::SplatCloud& cloud, int maxShDegree,
                                        bool cpuOrder = true) const;
  // An empty world of `capacity` records at `shDegree`, the slab the tiles of a tiled
  // world land in; nothing is drawn until an order names records that were uploaded.
  std::unique_ptr<GpuWorld> createSlab(uint32_t capacity, int shDegree, bool cpuOrder = true) const;
  // Converts and uploads a tile into records [offset, offset + count) of a slab
  // (blocking). Harmonics above the slab's degree are dropped, missing ones are zero.
  static bool uploadTile(const GpuWorld& slab, uint32_t offset, const splat::SplatCloud& cloud);

  // Points the descriptor set of every frame slot at this world's buffers.
  void bindWorld(const GpuWorld& world);

  // After the frame-slot fence, before compute/draw. Borrowed uniform stays valid until
  // this pipeline is destroyed; order must cover capacity uint32s and remain alive.
  VkBuffer updateCamera(uint32_t frameSlot, const splat::Mat4& view, const splat::Mat4& proj,
                        const splat::Vec3& cameraPosition, VkExtent2D extent);
  void bindOrder(uint32_t frameSlot, VkBuffer order, uint32_t capacity);
  void drawIndirect(VkCommandBuffer cmd, uint32_t frameSlot, const GpuWorld& world, int shDegree,
                    VkBuffer arguments);

  // Records the copy of a new draw order into the world. Must be called outside a render
  // pass, before `draw` in the same command buffer. `order` has `count` entries, at most
  // `world.count`: the sorter leaves out what the frustum cannot see.
  static void updateOrder(VkCommandBuffer cmd, uint32_t frameSlot, const GpuWorld& world,
                          const uint32_t* order, uint32_t count);

  // Draws the first `count` entries of the order buffer with the spherical harmonics
  // bands up to `shDegree`, which must not exceed what the world was uploaded with.
  void draw(VkCommandBuffer cmd, uint32_t frameSlot, const GpuWorld& world, uint32_t count,
            int shDegree, const splat::Mat4& view, const splat::Mat4& proj,
            const splat::Vec3& cameraPosition, VkExtent2D extent);

  static constexpr int kMaxShDegree = 3;

 private:
  explicit SplatPipeline(const VulkanContext& ctx) : ctx_(ctx) {}
  bool createDescriptors();
  bool createPipelines(VkRenderPass renderPass);

  const VulkanContext& ctx_;
  bool outputLinear_ = true;
  VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
  VkDescriptorPool pool_ = VK_NULL_HANDLE;
  std::array<VkDescriptorSet, FrameLoop::kFramesInFlight> sets_{};
  std::array<std::unique_ptr<GpuBuffer>, FrameLoop::kFramesInFlight> uniforms_{};
  VkPipelineLayout layout_ = VK_NULL_HANDLE;
  // One pipeline per SH degree, specialised so degree 0 worlds pay nothing for SH.
  std::array<VkPipeline, kMaxShDegree + 1> pipelines_{};
};

}  // namespace splatkit
