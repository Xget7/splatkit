#pragma once

#include <cstdint>
#include <vector>

#include "splat/math/Mat4.h"

namespace splat {

// Orders splats back to front by Euclidean distance from a point.
//
// Distance rather than view depth on purpose: the order only depends on where the
// camera is, not where it looks, so turning (gyroscope, drag) never triggers a sort.
// Only translation does. The sort is an LSD radix sort on the float bits of the squared
// distance: 4 passes of 8 bits, linear time, no comparisons.
class DistanceSorter {
 public:
  // Keeps a copy of the positions (xyz per splat) so the caller's cloud may go away.
  explicit DistanceSorter(std::vector<float> positions);

  std::size_t count() const { return positions_.size() / 3; }

  // Fills `order` with every splat index, farthest first.
  void sort(Vec3 from, std::vector<uint32_t>& order);

 private:
  std::vector<float> positions_;
  std::vector<uint32_t> keys_;
  std::vector<uint32_t> keysScratch_;
  std::vector<uint32_t> orderScratch_;
};

}  // namespace splat
