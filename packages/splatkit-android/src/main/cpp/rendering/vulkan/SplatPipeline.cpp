#include "rendering/vulkan/SplatPipeline.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "Log.h"
#include "splat/math/Half.h"
#include "shaders/splat_frag.h"
#include "shaders/splat_vert.h"

namespace splatkit {
namespace {

VkShaderModule makeModule(VkDevice device, const uint32_t* code, size_t size) {
  VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  info.codeSize = size;
  info.pCode = code;
  VkShaderModule module = VK_NULL_HANDLE;
  vkCreateShaderModule(device, &info, nullptr, &module);
  return module;
}

uint32_t packRgba8(float r, float g, float b, float a) {
  auto q = [](float v) { return static_cast<uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
  return q(r) | (q(g) << 8) | (q(b) << 16) | (q(a) << 24);
}

uint32_t packHalf2(float a, float b) {
  return static_cast<uint32_t>(splat::toHalf(a)) | (static_cast<uint32_t>(splat::toHalf(b)) << 16);
}

}  // namespace

splat::Result<std::unique_ptr<SplatPipeline>> SplatPipeline::create(const VulkanContext& ctx,
                                                                    VkRenderPass renderPass,
                                                                    bool swapchainIsSrgb) {
  std::unique_ptr<SplatPipeline> p(new SplatPipeline(ctx));
  p->outputLinear_ = swapchainIsSrgb;
  if (!p->createDescriptors()) {
    return splat::Error{splat::ErrorCode::gpuUnavailable, "splat descriptors"};
  }
  if (!p->createPipelines(renderPass)) {
    return splat::Error{splat::ErrorCode::gpuUnavailable, "splat pipeline"};
  }
  return p;
}

SplatPipeline::~SplatPipeline() {
  VkDevice device = ctx_.device();
  for (VkPipeline pipeline : pipelines_) {
    if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
  }
  if (layout_) vkDestroyPipelineLayout(device, layout_, nullptr);
  if (pool_) vkDestroyDescriptorPool(device, pool_, nullptr);
  if (setLayout_) vkDestroyDescriptorSetLayout(device, setLayout_, nullptr);
}

// Set 0: binding 0 camera (uniform), 1 splats, 2 draw order, 3 spherical harmonics
// (storage). One set per frame in flight so a uniform update never races the GPU.
bool SplatPipeline::createDescriptors() {
  VkDevice device = ctx_.device();

  VkDescriptorSetLayoutBinding bindings[4]{};
  bindings[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
  bindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
  bindings[2] = {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
  bindings[3] = {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
  VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  layoutInfo.bindingCount = 4;
  layoutInfo.pBindings = bindings;
  if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &setLayout_) != VK_SUCCESS) return false;

  VkDescriptorPoolSize sizes[2]{};
  sizes[0] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, FrameLoop::kFramesInFlight};
  sizes[1] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 * FrameLoop::kFramesInFlight};
  VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  poolInfo.maxSets = FrameLoop::kFramesInFlight;
  poolInfo.poolSizeCount = 2;
  poolInfo.pPoolSizes = sizes;
  if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool_) != VK_SUCCESS) return false;

  std::array<VkDescriptorSetLayout, FrameLoop::kFramesInFlight> layouts;
  layouts.fill(setLayout_);
  VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocInfo.descriptorPool = pool_;
  allocInfo.descriptorSetCount = FrameLoop::kFramesInFlight;
  allocInfo.pSetLayouts = layouts.data();
  if (vkAllocateDescriptorSets(device, &allocInfo, sets_.data()) != VK_SUCCESS) return false;

  for (uint32_t i = 0; i < FrameLoop::kFramesInFlight; ++i) {
    uniforms_[i] = GpuBuffer::hostVisible(ctx_, sizeof(CameraUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    if (!uniforms_[i]) return false;
    VkDescriptorBufferInfo info{uniforms_[i]->handle(), 0, sizeof(CameraUniform)};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = sets_[i];
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &info;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
  }
  return true;
}

bool SplatPipeline::createPipelines(VkRenderPass renderPass) {
  VkDevice device = ctx_.device();
  VkShaderModule vert = makeModule(device, shaders::splat_vert, shaders::splat_vert_size);
  VkShaderModule frag = makeModule(device, shaders::splat_frag, shaders::splat_frag_size);
  if (vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE) return false;

  // constant_id 0 of the vertex shader is the SH degree; each pipeline gets its own.
  uint32_t shDegree = 0;
  VkSpecializationMapEntry entry{0, 0, sizeof(uint32_t)};
  VkSpecializationInfo specialization{1, &entry, sizeof(shDegree), &shDegree};

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vert;
  stages[0].pName = "main";
  stages[0].pSpecializationInfo = &specialization;
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = frag;
  stages[1].pName = "main";

  // No vertex buffers: everything is fetched from storage buffers by instance index.
  VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

  VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewport.viewportCount = 1;
  viewport.scissorCount = 1;
  VkDynamicState dynamics[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamic.dynamicStateCount = 2;
  dynamic.pDynamicStates = dynamics;

  VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode = VK_CULL_MODE_NONE;
  raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  // "Over" compositing, back to front: out = src.a * src + (1 - src.a) * dst.
  VkPipelineColorBlendAttachmentState blendAttachment{};
  blendAttachment.blendEnable = VK_TRUE;
  blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
  blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
  blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  blend.attachmentCount = 1;
  blend.pAttachments = &blendAttachment;

  VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layoutInfo.setLayoutCount = 1;
  layoutInfo.pSetLayouts = &setLayout_;
  if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout_) != VK_SUCCESS) return false;

  VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  info.stageCount = 2;
  info.pStages = stages;
  info.pVertexInputState = &vertexInput;
  info.pInputAssemblyState = &assembly;
  info.pViewportState = &viewport;
  info.pRasterizationState = &raster;
  info.pMultisampleState = &multisample;
  info.pColorBlendState = &blend;
  info.pDynamicState = &dynamic;
  info.layout = layout_;
  info.renderPass = renderPass;
  info.subpass = 0;
  bool ok = true;
  for (int degree = 0; degree <= kMaxShDegree && ok; ++degree) {
    shDegree = static_cast<uint32_t>(degree);
    ok = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr,
                                   &pipelines_[static_cast<size_t>(degree)]) == VK_SUCCESS;
  }
  vkDestroyShaderModule(device, vert, nullptr);
  vkDestroyShaderModule(device, frag, nullptr);
  return ok;
}

namespace {

// Bands 1 to `degree` of every splat as halves, channel fastest, two per uint. The source
// keeps its own degree's coefficients per splat; a lower target degree keeps the leading
// ones, which is exactly the lower degree expansion.
std::vector<uint32_t> packSh(const splat::SplatCloud& cloud, int degree) {
  const size_t n = cloud.count();
  const size_t sourceCoefficients = n == 0 ? 0 : cloud.sh.size() / (n * 3);
  const size_t coefficients = static_cast<size_t>((degree + 1) * (degree + 1) - 1);
  const size_t halves = coefficients * 3;
  const size_t stride = (halves + 1) / 2;
  std::vector<uint32_t> packed(n * stride, 0);
  for (size_t i = 0; i < n; ++i) {
    const float* src = &cloud.sh[i * sourceCoefficients * 3];
    for (size_t h = 0; h < halves; ++h) {
      const uint32_t half = splat::toHalf(src[h]);
      packed[i * stride + h / 2] |= half << ((h & 1) * 16);
    }
  }
  return packed;
}

}  // namespace

std::unique_ptr<GpuWorld> SplatPipeline::uploadWorld(const splat::SplatCloud& cloud,
                                                     int maxShDegree) const {
  const size_t n = cloud.count();
  const int shDegree = std::clamp(std::min(cloud.shDegree, maxShDegree), 0, kMaxShDegree);
  const bool shComplete = cloud.sh.size() >= n * 3 * static_cast<size_t>((cloud.shDegree + 1) * (cloud.shDegree + 1) - 1);
  std::vector<uint32_t> sh = (shDegree > 0 && shComplete) ? packSh(cloud, shDegree) : std::vector<uint32_t>{0};
  std::vector<GpuSplat> packed(n);
  for (size_t i = 0; i < n; ++i) {
    GpuSplat& g = packed[i];
    std::memcpy(g.position, &cloud.positions[i * 3], sizeof(g.position));
    const float alpha = cloud.alphas[i];
    g.rgba8 = packRgba8(cloud.colors[i * 3], cloud.colors[i * 3 + 1], cloud.colors[i * 3 + 2], alpha);
    if (alpha > 1.0f) std::memcpy(&g.lodAlpha, &alpha, sizeof(g.lodAlpha));
    const float* c = &cloud.covariances[i * 6];  // xx, xy, xz, yy, yz, zz
    g.cov[0] = packHalf2(c[0], c[1]);
    g.cov[1] = packHalf2(c[2], c[3]);
    g.cov[2] = packHalf2(c[4], c[5]);
    if (alpha <= 1.0f) g.lodAlpha = 0;
  }
  // Identity order until the sorter runs.
  std::vector<uint32_t> order(n);
  for (uint32_t i = 0; i < n; ++i) order[i] = i;

  auto world = std::make_unique<GpuWorld>();
  world->count = static_cast<uint32_t>(n);
  world->shDegree = sh.size() > 1 ? shDegree : 0;
  world->splats = GpuBuffer::deviceLocal(ctx_, packed.size() * sizeof(GpuSplat),
                                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  world->order = GpuBuffer::deviceLocal(ctx_, order.size() * sizeof(uint32_t),
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  world->sh = GpuBuffer::deviceLocal(ctx_, sh.size() * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  if (!world->splats || !world->order || !world->sh) return nullptr;
  for (auto& staging : world->orderStaging) {
    staging = GpuBuffer::hostVisible(ctx_, order.size() * sizeof(uint32_t),
                                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    if (!staging) return nullptr;
  }
  if (!world->splats->upload(packed.data(), packed.size() * sizeof(GpuSplat))) return nullptr;
  if (!world->order->upload(order.data(), order.size() * sizeof(uint32_t))) return nullptr;
  if (!world->sh->upload(sh.data(), sh.size() * sizeof(uint32_t))) return nullptr;
  return world;
}

void SplatPipeline::bindWorld(const GpuWorld& world) {
  for (uint32_t i = 0; i < FrameLoop::kFramesInFlight; ++i) {
    VkDescriptorBufferInfo splats{world.splats->handle(), 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo order{world.order->handle(), 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo sh{world.sh->handle(), 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet writes[3]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = sets_[i];
    writes[0].dstBinding = 1;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].pBufferInfo = &splats;
    writes[1] = writes[0];
    writes[1].dstBinding = 2;
    writes[1].pBufferInfo = &order;
    writes[2] = writes[0];
    writes[2].dstBinding = 3;
    writes[2].pBufferInfo = &sh;
    vkUpdateDescriptorSets(ctx_.device(), 3, writes, 0, nullptr);
  }
}

void SplatPipeline::updateOrder(VkCommandBuffer cmd, uint32_t frameSlot, const GpuWorld& world,
                                const uint32_t* order, uint32_t count) const {
  const VkDeviceSize bytes = std::min(count, world.count) * sizeof(uint32_t);
  if (bytes == 0) return;
  GpuBuffer& staging = *world.orderStaging[frameSlot];
  std::memcpy(staging.mapped(), order, static_cast<size_t>(bytes));
  staging.flush(0, bytes);

  // The previous frame may still be reading the order buffer in its vertex shader.
  // Queue order alone does not prevent the copy from overlapping it (write after read),
  // so this barrier makes the transfer wait for those reads.
  VkBufferMemoryBarrier beforeCopy{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  beforeCopy.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
  beforeCopy.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  beforeCopy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  beforeCopy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  beforeCopy.buffer = world.order->handle();
  beforeCopy.offset = 0;
  beforeCopy.size = bytes;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                       0, nullptr, 1, &beforeCopy, 0, nullptr);

  VkBufferCopy region{0, 0, bytes};
  vkCmdCopyBuffer(cmd, staging.handle(), world.order->handle(), 1, &region);

  // And this frame's vertex shader must see the copy complete (read after write).
  VkBufferMemoryBarrier afterCopy = beforeCopy;
  afterCopy.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  afterCopy.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0,
                       0, nullptr, 1, &afterCopy, 0, nullptr);
}

void SplatPipeline::draw(VkCommandBuffer cmd, uint32_t frameSlot, const GpuWorld& world, uint32_t count,
                         const splat::Mat4& view, const splat::Mat4& proj,
                         const splat::Vec3& cameraPosition, VkExtent2D extent) {
  if (count == 0) return;
  CameraUniform u{};
  u.cameraPosition[0] = cameraPosition.x;
  u.cameraPosition[1] = cameraPosition.y;
  u.cameraPosition[2] = cameraPosition.z;
  u.view = view;
  u.proj = proj;
  u.screenSize[0] = static_cast<float>(extent.width);
  u.screenSize[1] = static_cast<float>(extent.height);
  u.focal[0] = u.screenSize[0] * proj.at(0, 0) / 2;
  u.focal[1] = u.screenSize[1] * proj.at(1, 1) / 2;
  u.tanHalfFov[0] = 1 / proj.at(0, 0);
  u.tanHalfFov[1] = 1 / proj.at(1, 1);
  u.outputLinear = outputLinear_ ? 1u : 0u;
  std::memcpy(uniforms_[frameSlot]->mapped(), &u, sizeof(u));
  uniforms_[frameSlot]->flush(0, sizeof(u));

  const int degree = std::clamp(world.shDegree, 0, kMaxShDegree);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines_[static_cast<size_t>(degree)]);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &sets_[frameSlot], 0, nullptr);
  vkCmdDraw(cmd, 4, std::min(count, world.count), 0, 0);
}

}  // namespace splatkit
