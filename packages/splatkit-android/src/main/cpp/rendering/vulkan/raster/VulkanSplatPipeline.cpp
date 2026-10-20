#include "rendering/vulkan/raster/VulkanSplatPipeline.h"

#include <algorithm>

#include "rendering/vulkan/device/VulkanShaderModule.h"
#include "rendering/vulkan/layout/VulkanShaderTypes.h"
#include "shaders/splat_frag.h"
#include "shaders/splat_vert.h"
#include "splatkit/Log.h"

namespace splatkit {
namespace {

// The bindings of set 0 in splat.vert.
constexpr uint32_t kCameraBinding = 0;
constexpr uint32_t kSplatsBinding = 1;
constexpr uint32_t kOrderBinding = 2;
constexpr uint32_t kShBinding = 3;
constexpr uint32_t kBindings = 4;
constexpr uint32_t kStorageBindings = kBindings - 1;

}  // namespace

splat::Result<std::unique_ptr<VulkanSplatPipeline>> VulkanSplatPipeline::create(
    const VulkanContext& ctx, VkRenderPass renderPass, const VulkanCameraUniforms& cameras) {
  std::unique_ptr<VulkanSplatPipeline> p(new VulkanSplatPipeline(ctx));
  if (!p->createDescriptors(cameras)) {
    return splat::Error{splat::ErrorCode::gpuUnavailable, "splat descriptors"};
  }
  if (!p->createPipelines(renderPass)) {
    return splat::Error{splat::ErrorCode::gpuUnavailable, "splat pipeline"};
  }
  return p;
}

VulkanSplatPipeline::~VulkanSplatPipeline() {
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
bool VulkanSplatPipeline::createDescriptors(const VulkanCameraUniforms& cameras) {
  VkDevice device = ctx_.device();

  VkDescriptorSetLayoutBinding bindings[kBindings]{};
  bindings[kCameraBinding] = {kCameraBinding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                              VK_SHADER_STAGE_VERTEX_BIT, nullptr};
  for (const uint32_t binding : {kSplatsBinding, kOrderBinding, kShBinding}) {
    bindings[binding] = {binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT,
                         nullptr};
  }
  VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  layoutInfo.bindingCount = kBindings;
  layoutInfo.pBindings = bindings;
  if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &setLayout_) != VK_SUCCESS)
    return false;

  VkDescriptorPoolSize sizes[2]{};
  sizes[0] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kFramesInFlight};
  sizes[1] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kStorageBindings * kFramesInFlight};
  VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  poolInfo.maxSets = kFramesInFlight;
  poolInfo.poolSizeCount = 2;
  poolInfo.pPoolSizes = sizes;
  if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool_) != VK_SUCCESS) return false;

  std::array<VkDescriptorSetLayout, kFramesInFlight> layouts;
  layouts.fill(setLayout_);
  VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocInfo.descriptorPool = pool_;
  allocInfo.descriptorSetCount = kFramesInFlight;
  allocInfo.pSetLayouts = layouts.data();
  if (vkAllocateDescriptorSets(device, &allocInfo, sets_.data()) != VK_SUCCESS) return false;

  for (uint32_t i = 0; i < kFramesInFlight; ++i) {
    const VkDescriptorBufferInfo info{cameras.buffer(i), 0, sizeof(CameraUniform)};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = sets_[i];
    write.dstBinding = kCameraBinding;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &info;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
  }
  return true;
}

bool VulkanSplatPipeline::createPipelines(VkRenderPass renderPass) {
  VkDevice device = ctx_.device();
  const ShaderModule vert(device, shaders::splat_vert, shaders::splat_vert_size);
  const ShaderModule frag(device, shaders::splat_frag, shaders::splat_frag_size);
  if (!vert || !frag) return false;

  // constant_id 0 of the vertex shader is the SH degree; each pipeline gets its own.
  uint32_t shDegree = 0;
  const VkSpecializationMapEntry entry{0, 0, sizeof(uint32_t)};
  const VkSpecializationInfo specialization{1, &entry, sizeof(shDegree), &shDegree};

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vert.get();
  stages[0].pName = "main";
  stages[0].pSpecializationInfo = &specialization;
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = frag.get();
  stages[1].pName = "main";

  // No vertex buffers: everything is fetched from storage buffers by instance index.
  const VkPipelineVertexInputStateCreateInfo vertexInput{
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo assembly{
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

  VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewport.viewportCount = 1;
  viewport.scissorCount = 1;
  VkDynamicState dynamics[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamic.dynamicStateCount = 2;
  dynamic.pDynamicStates = dynamics;

  VkPipelineRasterizationStateCreateInfo raster{
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode = VK_CULL_MODE_NONE;
  raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisample{
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
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
  VkPipelineColorBlendStateCreateInfo blend{
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
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
  for (int degree = 0; degree <= VulkanWorld::kMaxShDegree && ok; ++degree) {
    shDegree = static_cast<uint32_t>(degree);
    ok = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr,
                                   &pipelines_[static_cast<size_t>(degree)]) == VK_SUCCESS;
  }
  return ok;
}

void VulkanSplatPipeline::bindWorld(const VulkanWorld& world) {
  for (uint32_t i = 0; i < kFramesInFlight; ++i) {
    const VkDescriptorBufferInfo splats{world.splats->handle(), 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo order{world.order->handle(), 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo sh{world.sh->handle(), 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet writes[3]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = sets_[i];
    writes[0].dstBinding = kSplatsBinding;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].pBufferInfo = &splats;
    writes[1] = writes[0];
    writes[1].dstBinding = kOrderBinding;
    writes[1].pBufferInfo = &order;
    writes[2] = writes[0];
    writes[2].dstBinding = kShBinding;
    writes[2].pBufferInfo = &sh;
    vkUpdateDescriptorSets(ctx_.device(), 3, writes, 0, nullptr);
  }
}

void VulkanSplatPipeline::bindOrder(uint32_t frameSlot, VkBuffer order, uint32_t capacity) {
  const VkDescriptorBufferInfo info{order, 0,
                                    VkDeviceSize{std::max(1u, capacity)} * sizeof(uint32_t)};
  VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  write.dstSet = sets_[frameSlot];
  write.dstBinding = kOrderBinding;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  write.pBufferInfo = &info;
  vkUpdateDescriptorSets(ctx_.device(), 1, &write, 0, nullptr);
}

void VulkanSplatPipeline::bindDraw(VkCommandBuffer cmd, uint32_t frameSlot,
                                   const VulkanWorld& world, int shDegree) const {
  const int degree = std::clamp(std::min(shDegree, world.shDegree), 0, VulkanWorld::kMaxShDegree);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines_[static_cast<size_t>(degree)]);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &sets_[frameSlot], 0,
                          nullptr);
}

void VulkanSplatPipeline::drawIndirect(VkCommandBuffer cmd, uint32_t frameSlot,
                                       const VulkanWorld& world, int shDegree, VkBuffer arguments) {
  bindDraw(cmd, frameSlot, world, shDegree);
  vkCmdDrawIndirect(cmd, arguments, 0, 1, sizeof(VkDrawIndirectCommand));
}

void VulkanSplatPipeline::draw(VkCommandBuffer cmd, uint32_t frameSlot, const VulkanWorld& world,
                               uint32_t count, int shDegree) {
  if (count == 0) return;
  bindDraw(cmd, frameSlot, world, shDegree);
  vkCmdDraw(cmd, 4, std::min(count, world.count), 0, 0);
}

}  // namespace splatkit
