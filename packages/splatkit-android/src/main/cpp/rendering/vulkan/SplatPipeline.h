#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include <vulkan/vulkan.h>

#include "rendering/vulkan/FrameLoop.h"
#include "rendering/vulkan/GpuBuffer.h"
#include "rendering/vulkan/VulkanContext.h"
#include "splat/core/Result.h"
#include "splat/formats/SplatCloud.h"
#include "splat/math/Mat4.h"
#include "splat/math/Vec3.h"

namespace splatkit {

// Exactly the layout the vertex shader reads (std430, 32 bytes).
// Vertex fetch is the floor of the frame on Adreno 640 (8 ms for 500k splats at 48
// bytes), so the record is as small as the source data allows: SPZ stores colour and
// alpha as 8 bits, and the covariance keeps 11 bits of mantissa as half floats.
struct GpuSplat {
  float position[3];
  uint32_t rgba8;    // colour and alpha, a real uint: never routed through a float, whose
                     // NaN patterns some mobile compilers canonicalise
  uint32_t cov[3];   // six halves: (xx, xy), (xz, yy), (yz, zz)
  uint32_t lodAlpha; // float bits of an opacity above 1 (level of detail nodes), else 0
};
static_assert(sizeof(GpuSplat) == 32, "GpuSplat must match the shader struct");

// std140 layout of the Camera uniform block.
struct CameraUniform {
  splat::Mat4 view;
  splat::Mat4 proj;
  float focal[2];
  float tanHalfFov[2];
  float screenSize[2];
  uint32_t outputLinear;
  uint32_t pad;
  float cameraPosition[4];
};

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
  std::unique_ptr<GpuWorld> uploadWorld(const splat::SplatCloud& cloud, int maxShDegree) const;

  // Points the descriptor set of every frame slot at this world's buffers.
  void bindWorld(const GpuWorld& world);

  // Records the copy of a new draw order into the world. Must be called outside a render
  // pass, before `draw` in the same command buffer. `order` has `count` entries, at most
  // `world.count`: the sorter leaves out what the frustum cannot see.
  void updateOrder(VkCommandBuffer cmd, uint32_t frameSlot, const GpuWorld& world,
                   const uint32_t* order, uint32_t count) const;

  // Draws the first `count` entries of the order buffer.
  void draw(VkCommandBuffer cmd, uint32_t frameSlot, const GpuWorld& world, uint32_t count,
            const splat::Mat4& view, const splat::Mat4& proj, const splat::Vec3& cameraPosition,
            VkExtent2D extent);

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
