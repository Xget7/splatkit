#pragma once

#import <Metal/Metal.h>

#include <array>
#include <cstdint>
#include <vector>

#include "splatkit/rendering/SplatRenderer.h"

namespace splatkit {

// The visible order of a frame, made on the GPU: the culling and the back to front sort
// of the splats in the slab ranges to draw, as compute passes ahead of the draw. Owns
// the key and value buffers, the histograms and the indirect arguments; the caller owns
// the command buffers. See the kernels in Splat.metal.
class MetalVisibility {
 public:
  // Pipelines from the library the kernels live in. False when one is missing.
  bool create(id<MTLDevice> device, id<MTLLibrary> library);
  // Buffers for up to `capacity` pairs; the buffers of a smaller capacity are replaced.
  bool reserve(uint32_t capacity);
  uint32_t capacity() const { return capacity_; }

  // Encodes the whole thing: the cull of `ranges` from the camera in `uniforms`, then the
  // sort. `order()` and `drawArguments()` are valid once the command buffer completes.
  void encode(id<MTLCommandBuffer> cmd, id<MTLBuffer> uniforms, id<MTLBuffer> splats,
              const SplatRenderer::Range* ranges, uint32_t rangeCount);

  // The sort alone, of the first `count()` pairs of `keys()` and `values()`, ascending by
  // key; the count is read from `countBuffer()`. For tests, and used by `encode`.
  void encodeSort(id<MTLCommandBuffer> cmd);

  id<MTLBuffer> keys() const { return keys_[0]; }
  id<MTLBuffer> values() const { return values_[0]; }
  id<MTLBuffer> countBuffer() const { return count_; }
  // The sorted slab indices, farthest first; `count()` of them.
  id<MTLBuffer> order() const { return values_[0]; }
  id<MTLBuffer> drawArguments() const { return drawArguments_; }
  uint32_t count() const { return *static_cast<const uint32_t*>(count_.contents); }

  static constexpr uint32_t kThreads = 256;
  static constexpr uint32_t kBlock = kThreads * 16;
  static constexpr uint32_t kBins = 16;
  static constexpr uint32_t kPasses = 8;
  static constexpr uint32_t kMaxRanges = 65536;

 private:
  id<MTLDevice> device_ = nil;
  id<MTLComputePipelineState> visibility_ = nil;
  id<MTLComputePipelineState> prepare_ = nil;
  id<MTLComputePipelineState> histogram_ = nil;
  id<MTLComputePipelineState> scan_ = nil;
  id<MTLComputePipelineState> scatter_ = nil;

  uint32_t capacity_ = 0;
  std::array<id<MTLBuffer>, 2> keys_{};
  std::array<id<MTLBuffer>, 2> values_{};
  id<MTLBuffer> histogram_buffer_ = nil;
  id<MTLBuffer> totals_ = nil;
  id<MTLBuffer> count_ = nil;
  id<MTLBuffer> dispatch_ = nil;
  id<MTLBuffer> drawArguments_ = nil;
  id<MTLBuffer> ranges_ = nil;
  id<MTLBuffer> rangeStarts_ = nil;
  std::vector<uint32_t> starts_;
};

}  // namespace splatkit
