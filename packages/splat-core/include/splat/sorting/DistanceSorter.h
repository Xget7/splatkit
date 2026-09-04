#pragma once

#include <cstdint>
#include <vector>

#include "splat/math/Frustum.h"
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

  // Like `sort`, but only splats inside the frustum enter `order`, which is resized to
  // the count returned. The GPU then never sees what is behind or beside the camera.
  std::size_t sortVisible(const Frustum& frustum, std::vector<uint32_t>& order);

 private:
  // Sorts the first n entries of keys_ and order together, ascending by key.
  void radixSort(std::size_t n, std::vector<uint32_t>& order);

  std::vector<float> positions_;
  std::vector<uint32_t> keys_;
  std::vector<uint32_t> keysScratch_;
  std::vector<uint32_t> orderScratch_;
};

}  // namespace splat
