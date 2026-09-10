#include "rendering/MetalVisibility.h"

#include <algorithm>
#include <cstring>

#include "splatkit/Log.h"

namespace splatkit {
namespace {

id<MTLComputePipelineState> pipeline(id<MTLDevice> device, id<MTLLibrary> library,
                                     const char* name) {
  id<MTLFunction> function = [library newFunctionWithName:@(name)];
  if (function == nil) {
    LOGE("kernel %s missing", name);
    return nil;
  }
  NSError* error = nil;
  id<MTLComputePipelineState> state = [device newComputePipelineStateWithFunction:function
                                                                            error:&error];
  if (state == nil) LOGE("kernel %s: %s", name, error.localizedDescription.UTF8String);
  return state;
}

id<MTLBuffer> buffer(id<MTLDevice> device, size_t bytes) {
  return [device newBufferWithLength:std::max<size_t>(bytes, 16)
                             options:MTLResourceStorageModeShared];
}

}  // namespace

bool MetalVisibility::create(id<MTLDevice> device, id<MTLLibrary> library) {
  device_ = device;
  visibility_ = pipeline(device, library, "visibility");
  prepare_ = pipeline(device, library, "prepareSort");
  histogram_ = pipeline(device, library, "radixHistogram");
  scan_ = pipeline(device, library, "radixScan");
  scatter_ = pipeline(device, library, "radixScatter");
  count_ = buffer(device, sizeof(uint32_t));
  dispatch_ = buffer(device, 4 * sizeof(uint32_t));
  drawArguments_ = buffer(device, 4 * sizeof(uint32_t));
  totals_ = buffer(device, kBins * sizeof(uint32_t));
  ranges_ = buffer(device, size_t{kMaxRanges} * 2 * sizeof(uint32_t));
  rangeStarts_ = buffer(device, size_t{kMaxRanges + 1} * sizeof(uint32_t));
  return visibility_ != nil && prepare_ != nil && histogram_ != nil && scan_ != nil &&
         scatter_ != nil;
}

bool MetalVisibility::reserve(uint32_t capacity) {
  if (capacity <= capacity_) return true;
  const uint32_t blocks = (capacity + kBlock - 1) / kBlock;
  for (auto& k : keys_) k = buffer(device_, size_t{capacity} * sizeof(uint32_t));
  for (auto& v : values_) v = buffer(device_, size_t{capacity} * sizeof(uint32_t));
  histogram_buffer_ = buffer(device_, size_t{blocks} * kBins * sizeof(uint32_t));
  if (keys_[0] == nil || keys_[1] == nil || values_[0] == nil || values_[1] == nil ||
      histogram_buffer_ == nil) {
    LOGE("visibility buffers for %u splats failed", capacity);
    capacity_ = 0;
    return false;
  }
  capacity_ = capacity;
  return true;
}

void MetalVisibility::encode(id<MTLCommandBuffer> cmd, id<MTLBuffer> uniforms, id<MTLBuffer> splats,
                             const SplatRenderer::Range* ranges, uint32_t rangeCount) {
  rangeCount = std::min(rangeCount, kMaxRanges);
  starts_.resize(size_t{rangeCount} + 1);
  uint32_t total = 0;
  auto* rangeOut = static_cast<uint32_t*>(ranges_.contents);
  for (uint32_t i = 0; i < rangeCount; ++i) {
    starts_[i] = total;
    total += std::min(ranges[i].count, capacity_ - std::min(ranges[i].offset, capacity_));
    rangeOut[i * 2] = ranges[i].offset;
    rangeOut[i * 2 + 1] = ranges[i].count;
  }
  starts_[rangeCount] = total;
  std::memcpy(rangeStarts_.contents, starts_.data(), starts_.size() * sizeof(uint32_t));
  *static_cast<uint32_t*>(count_.contents) = 0;

  id<MTLComputeCommandEncoder> cull = [cmd computeCommandEncoder];
  [cull setComputePipelineState:visibility_];
  [cull setBuffer:uniforms offset:0 atIndex:0];
  [cull setBuffer:splats offset:0 atIndex:1];
  [cull setBuffer:ranges_ offset:0 atIndex:2];
  [cull setBuffer:rangeStarts_ offset:0 atIndex:3];
  [cull setBytes:&rangeCount length:sizeof(rangeCount) atIndex:4];
  [cull setBuffer:keys_[0] offset:0 atIndex:5];
  [cull setBuffer:values_[0] offset:0 atIndex:6];
  [cull setBuffer:count_ offset:0 atIndex:7];
  const NSUInteger groups = (std::max(total, 1u) + kThreads - 1) / kThreads;
  [cull dispatchThreadgroups:MTLSizeMake(groups, 1, 1)
       threadsPerThreadgroup:MTLSizeMake(kThreads, 1, 1)];
  [cull endEncoding];

  encodeSort(cmd);
}

void MetalVisibility::encodeSort(id<MTLCommandBuffer> cmd) {
  id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
  [enc setComputePipelineState:prepare_];
  [enc setBuffer:count_ offset:0 atIndex:0];
  [enc setBuffer:dispatch_ offset:0 atIndex:1];
  [enc setBuffer:drawArguments_ offset:0 atIndex:2];
  [enc dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];

  const MTLSize threads = MTLSizeMake(kThreads, 1, 1);
  for (uint32_t pass = 0; pass < kPasses; ++pass) {
    const uint32_t shift = pass * 4;
    const uint32_t in = pass & 1u;
    const uint32_t out = in ^ 1u;

    [enc setComputePipelineState:histogram_];
    [enc setBuffer:keys_[in] offset:0 atIndex:0];
    [enc setBuffer:count_ offset:0 atIndex:1];
    [enc setBuffer:dispatch_ offset:0 atIndex:2];
    [enc setBytes:&shift length:sizeof(shift) atIndex:3];
    [enc setBuffer:histogram_buffer_ offset:0 atIndex:4];
    [enc dispatchThreadgroupsWithIndirectBuffer:dispatch_
                           indirectBufferOffset:0
                          threadsPerThreadgroup:threads];

    [enc setComputePipelineState:scan_];
    [enc setBuffer:dispatch_ offset:0 atIndex:0];
    [enc setBuffer:histogram_buffer_ offset:0 atIndex:1];
    [enc setBuffer:totals_ offset:0 atIndex:2];
    [enc dispatchThreadgroups:MTLSizeMake(kBins, 1, 1) threadsPerThreadgroup:threads];

    [enc setComputePipelineState:scatter_];
    [enc setBuffer:keys_[in] offset:0 atIndex:0];
    [enc setBuffer:values_[in] offset:0 atIndex:1];
    [enc setBuffer:keys_[out] offset:0 atIndex:2];
    [enc setBuffer:values_[out] offset:0 atIndex:3];
    [enc setBuffer:count_ offset:0 atIndex:4];
    [enc setBuffer:dispatch_ offset:0 atIndex:5];
    [enc setBytes:&shift length:sizeof(shift) atIndex:6];
    [enc setBuffer:histogram_buffer_ offset:0 atIndex:7];
    [enc setBuffer:totals_ offset:0 atIndex:8];
    [enc dispatchThreadgroupsWithIndirectBuffer:dispatch_
                           indirectBufferOffset:0
                          threadsPerThreadgroup:threads];
  }
  [enc endEncoding];
}

}  // namespace splatkit
