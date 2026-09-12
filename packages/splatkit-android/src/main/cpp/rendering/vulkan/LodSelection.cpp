#include "rendering/vulkan/LodSelection.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "rendering/vulkan/VulkanShaderTypes.h"
#include "shaders/lod_selection_comp.h"
#include "splat/lod/LodFile.h"

namespace splatkit {
namespace {
constexpr uint32_t kThreads = 128;
constexpr uint32_t kMaxCapacity = 2200000;
void dependency(VkCommandBuffer cmd, VkPipelineStageFlags source, VkAccessFlags sourceAccess,
                VkPipelineStageFlags destination, VkAccessFlags destinationAccess) {
  VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  barrier.srcAccessMask = sourceAccess;
  barrier.dstAccessMask = destinationAccess;
  vkCmdPipelineBarrier(cmd, source, destination, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}
}  // namespace

splat::Result<std::unique_ptr<LodSelection>> LodSelection::create(const VulkanContext& ctx) {
  std::unique_ptr<LodSelection> pass(new LodSelection(ctx));
  if (!pass->initialize())
    return splat::Error{splat::ErrorCode::gpuUnavailable,
                        "LOD compute limits or pipeline unavailable"};
  return pass;
}

bool LodSelection::initialize() {
  static_assert(sizeof(Config) == 52);
  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(ctx_.physicalDevice(), &properties);
  limits_ = properties.limits;
  uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(ctx_.physicalDevice(), &count, nullptr);
  std::vector<VkQueueFamilyProperties> families(count);
  vkGetPhysicalDeviceQueueFamilyProperties(ctx_.physicalDevice(), &count, families.data());
  if (ctx_.queueFamily() >= count ||
      !(families[ctx_.queueFamily()].queueFlags & VK_QUEUE_COMPUTE_BIT) ||
      limits_.maxComputeWorkGroupInvocations < kThreads ||
      limits_.maxComputeWorkGroupSize[0] < kThreads ||
      limits_.maxComputeSharedMemorySize < kThreads * 16 ||
      limits_.maxPerStageDescriptorStorageBuffers < 4 ||
      limits_.maxDescriptorSetStorageBuffers < 4 ||
      limits_.maxPerStageDescriptorUniformBuffers < 1 ||
      limits_.maxDescriptorSetUniformBuffers < 1 || limits_.maxPerStageResources < 5 ||
      limits_.maxUniformBufferRange < sizeof(CameraUniform) ||
      limits_.maxPushConstantsSize < sizeof(Config))
    return false;

  VkDescriptorSetLayoutBinding bindings[5]{};
  bindings[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
  for (uint32_t i = 1; i < 5; ++i)
    bindings[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
  VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  set.bindingCount = 5;
  set.pBindings = bindings;
  if (vkCreateDescriptorSetLayout(ctx_.device(), &set, nullptr, &setLayout_) != VK_SUCCESS)
    return false;
  VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kSlots},
                                  {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4 * kSlots}};
  VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool.maxSets = kSlots;
  pool.poolSizeCount = 2;
  pool.pPoolSizes = sizes;
  if (vkCreateDescriptorPool(ctx_.device(), &pool, nullptr, &pool_) != VK_SUCCESS) return false;
  std::array<VkDescriptorSetLayout, kSlots> layouts{};
  layouts.fill(setLayout_);
  VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocate.descriptorPool = pool_;
  allocate.descriptorSetCount = kSlots;
  allocate.pSetLayouts = layouts.data();
  if (vkAllocateDescriptorSets(ctx_.device(), &allocate, sets_.data()) != VK_SUCCESS) return false;
  VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Config)};
  VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layout.setLayoutCount = 1;
  layout.pSetLayouts = &setLayout_;
  layout.pushConstantRangeCount = 1;
  layout.pPushConstantRanges = &push;
  if (vkCreatePipelineLayout(ctx_.device(), &layout, nullptr, &layout_) != VK_SUCCESS) return false;
  VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  moduleInfo.codeSize = shaders::lod_selection_comp_size;
  moduleInfo.pCode = shaders::lod_selection_comp;
  VkShaderModule module = VK_NULL_HANDLE;
  if (vkCreateShaderModule(ctx_.device(), &moduleInfo, nullptr, &module) != VK_SUCCESS)
    return false;
  VkComputePipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  pipeline.layout = layout_;
  pipeline.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
  pipeline.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  pipeline.stage.module = module;
  pipeline.stage.pName = "main";
  const auto result =
      vkCreateComputePipelines(ctx_.device(), VK_NULL_HANDLE, 1, &pipeline, nullptr, &pipeline_);
  vkDestroyShaderModule(ctx_.device(), module, nullptr);
  return result == VK_SUCCESS;
}

LodSelection::~LodSelection() {
  if (pipeline_) vkDestroyPipeline(ctx_.device(), pipeline_, nullptr);
  if (layout_) vkDestroyPipelineLayout(ctx_.device(), layout_, nullptr);
  if (pool_) vkDestroyDescriptorPool(ctx_.device(), pool_, nullptr);
  if (setLayout_) vkDestroyDescriptorSetLayout(ctx_.device(), setLayout_, nullptr);
}

bool LodSelection::upload(const splat::LodTree& tree, uint32_t budget, Quality quality) {
  if (budget == 0 || budget > kMaxCapacity || !std::isfinite(quality.pixelLimit) ||
      quality.pixelLimit < 0 || !std::isfinite(quality.colorWeight) || quality.colorWeight < 0)
    return false;
  const auto valid = splat::validateLodTree(tree);
  if (!valid || tree.nodeCount() > std::numeric_limits<uint32_t>::max()) return false;
  splat::LodSelectionData compatibility;
  const auto* data = &tree.selection;
  if (data->clusters.empty()) {
    compatibility = splat::buildLodSelectionData(tree);
    data = &compatibility;
  }
  if (data->clusters.empty() || data->leaves.empty()) return false;
  Config config;
  config.capacity = std::min(budget, static_cast<uint32_t>(tree.leafCount));
  config.pixelLimit = quality.pixelLimit;
  config.colorWeight = quality.colorWeight;
  config.cull = quality.frustumCull;
  const size_t frontier = std::min(size_t{config.capacity}, data->clusters.size());
  const size_t groups = (frontier + kThreads - 1) / kThreads;
  const size_t blocks = (groups + kThreads - 1) / kThreads;
  const size_t packets = std::min(data->clusters.size(), size_t{config.capacity} / (kThreads + 1));
  if (groups > limits_.maxComputeWorkGroupCount[0] || packets > limits_.maxComputeWorkGroupCount[0])
    return false;
  size_t words = 32;
  auto region = [&](size_t length) {
    const uint32_t start = static_cast<uint32_t>(words);
    words += length;
    return start;
  };
  config.costs = region(frontier * 2);
  config.offsets = region(frontier * 4);
  config.costGroups = region(groups * 4);
  config.groups = region(groups * 4);
  config.blocks = region((blocks + 1) * 4);
  config.frontier0 = region(frontier);
  config.frontier1 = region(frontier);
  config.packets = region(std::max(packets, size_t{1}) * 4);
  const VkDeviceSize clusterBytes = data->clusters.size() * sizeof(splat::LodCluster);
  const VkDeviceSize leafBytes = data->leaves.size() * sizeof(uint32_t);
  const VkDeviceSize scratchBytes = words * sizeof(uint32_t);
  const VkDeviceSize indexBytes = VkDeviceSize{config.capacity} * sizeof(uint32_t);
  if (words > std::numeric_limits<uint32_t>::max() ||
      std::max({clusterBytes, leafBytes, scratchBytes, indexBytes}) > limits_.maxStorageBufferRange)
    return false;
  constexpr auto storage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  auto clusters = GpuBuffer::deviceLocal(ctx_, clusterBytes, storage);
  auto leaves = GpuBuffer::deviceLocal(ctx_, leafBytes, storage);
  auto scratch = GpuBuffer::deviceLocal(
      ctx_, scratchBytes,
      storage | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
  auto indices =
      GpuBuffer::deviceLocal(ctx_, indexBytes, storage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
  if (!clusters || !leaves || !scratch || !indices ||
      !clusters->upload(data->clusters.data(), clusterBytes) ||
      !leaves->upload(data->leaves.data(), leafBytes))
    return false;
  clusters_ = std::move(clusters);
  leaves_ = std::move(leaves);
  scratch_ = std::move(scratch);
  indices_ = std::move(indices);
  config_ = config;
  rounds_ = valid.value() + 1;
  return true;
}

LodSelection::Output LodSelection::output() const {
  if (!scratch_) return {};
  return {indices_->handle(), scratch_->handle(), config_.capacity};
}

bool LodSelection::encode(VkCommandBuffer cmd, uint32_t slot, const Input& input) const {
  if (!cmd || slot >= kSlots || !scratch_ || !input.camera ||
      input.cameraOffset % limits_.minUniformBufferOffsetAlignment != 0 ||
      input.cameraOffset > std::numeric_limits<VkDeviceSize>::max() - sizeof(CameraUniform))
    return false;
  VkDescriptorBufferInfo buffers[] = {{input.camera, input.cameraOffset, sizeof(CameraUniform)},
                                      {clusters_->handle(), 0, clusters_->size()},
                                      {leaves_->handle(), 0, leaves_->size()},
                                      {scratch_->handle(), 0, scratch_->size()},
                                      {indices_->handle(), 0, indices_->size()}};
  VkWriteDescriptorSet writes[5]{};
  for (uint32_t i = 0; i < 5; ++i) {
    writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[i].dstSet = sets_[slot];
    writes[i].dstBinding = i;
    writes[i].descriptorCount = 1;
    writes[i].descriptorType =
        i == 0 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[i].pBufferInfo = &buffers[i];
  }
  vkUpdateDescriptorSets(ctx_.device(), 5, writes, 0, nullptr);
  // Inter-frame WAR/WAW plus upload/host writes, including consumers on previous submissions.
  dependency(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT,
             VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_HOST_WRITE_BIT,
             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
             VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_UNIFORM_READ_BIT);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout_, 0, 1, &sets_[slot], 0,
                          nullptr);
  auto dispatch = [&](uint32_t phase, VkDeviceSize indirectOffset = VK_WHOLE_SIZE) {
    Config config = config_;
    config.phase = phase;
    vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(config), &config);
    if (indirectOffset == VK_WHOLE_SIZE)
      vkCmdDispatch(cmd, 1, 1, 1);
    else
      vkCmdDispatchIndirect(cmd, scratch_->handle(), indirectOffset);
    dependency(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                   VK_ACCESS_INDIRECT_COMMAND_READ_BIT);
  };
  dispatch(0);
  for (uint32_t round = 0; round < rounds_; ++round) {
    dispatch(1, 32);
    dispatch(2, 64);
    dispatch(3);
    dispatch(4);
    dispatch(5, 32);
    dispatch(6, 64);
    dispatch(3);
    dispatch(7);
    dispatch(8, 32);
    dispatch(9);
  }
  dispatch(10, 44);
  dependency(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                 VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
             VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT |
                 VK_ACCESS_TRANSFER_READ_BIT);
  return true;
}
}  // namespace splatkit
