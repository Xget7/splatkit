#include "rendering/vulkan/DebugTrianglePipeline.h"

#include "shaders/triangle_frag.h"
#include "shaders/triangle_vert.h"

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

}  // namespace

splat::Result<std::unique_ptr<DebugTrianglePipeline>> DebugTrianglePipeline::create(
    const VulkanContext& ctx, VkRenderPass renderPass) {
  std::unique_ptr<DebugTrianglePipeline> p(new DebugTrianglePipeline(ctx));
  VkDevice device = ctx.device();

  VkShaderModule vert = makeModule(device, shaders::triangle_vert, shaders::triangle_vert_size);
  VkShaderModule frag = makeModule(device, shaders::triangle_frag, shaders::triangle_frag_size);
  if (vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE) {
    return splat::Error{splat::ErrorCode::gpuUnavailable, "triangle shader modules"};
  }

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vert;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = frag;
  stages[1].pName = "main";

  // No vertex buffers: positions come from gl_VertexIndex.
  const VkPipelineVertexInputStateCreateInfo vertexInput{
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo assembly{
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  // Viewport and scissor are dynamic so the pipeline survives a resize.
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

  VkPipelineColorBlendAttachmentState blendAttachment{};
  blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo blend{
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  blend.attachmentCount = 1;
  blend.pAttachments = &blendAttachment;

  const VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &p->layout_) != VK_SUCCESS) {
    return splat::Error{splat::ErrorCode::gpuUnavailable, "pipeline layout"};
  }

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
  info.layout = p->layout_;
  info.renderPass = renderPass;
  info.subpass = 0;

  const VkResult result =
      vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &p->pipeline_);
  vkDestroyShaderModule(device, vert, nullptr);
  vkDestroyShaderModule(device, frag, nullptr);
  if (result != VK_SUCCESS) {
    return splat::Error{splat::ErrorCode::gpuUnavailable, "triangle pipeline"};
  }
  return p;
}

DebugTrianglePipeline::~DebugTrianglePipeline() {
  if (pipeline_) vkDestroyPipeline(ctx_.device(), pipeline_, nullptr);
  if (layout_) vkDestroyPipelineLayout(ctx_.device(), layout_, nullptr);
}

void DebugTrianglePipeline::draw(VkCommandBuffer cmd) const {
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
  vkCmdDraw(cmd, 3, 1, 0, 0);
}

}  // namespace splatkit
