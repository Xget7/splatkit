#include "splat/tiles/SlabAllocator.h"

namespace splat {

SlabAllocator::SlabAllocator(std::uint32_t capacity) : capacity_(capacity) {
  if (capacity > 0) free_[0] = capacity;
}

std::optional<std::uint32_t> SlabAllocator::allocate(std::uint32_t count) {
  if (count == 0) return std::nullopt;
  for (auto it = free_.begin(); it != free_.end(); ++it) {
    if (it->second < count) continue;
    const std::uint32_t offset = it->first;
    const std::uint32_t left = it->second - count;
    free_.erase(it);
    if (left > 0) free_[offset + count] = left;
    used_ += count;
    return offset;
  }
  return std::nullopt;
}

void SlabAllocator::release(std::uint32_t offset, std::uint32_t count) {
  if (count == 0) return;
  used_ -= count;
  auto next = free_.lower_bound(offset);
  // Merge with the free range that ends where this one starts.
  if (next != free_.begin()) {
    auto prev = std::prev(next);
    if (prev->first + prev->second == offset) {
      offset = prev->first;
      count += prev->second;
      free_.erase(prev);
    }
  }
  // And with the one that starts where this one ends.
  if (next != free_.end() && next->first == offset + count) {
    count += next->second;
    free_.erase(next);
  }
  free_[offset] = count;
}

}  // namespace splat
