#include "splat/sorting/DistanceSorter.h"

#include <array>
#include <cstring>
#include <utility>

namespace splat {

DistanceSorter::DistanceSorter(std::vector<float> positions) : positions_(std::move(positions)) {
  const std::size_t n = count();
  keys_.resize(n);
  keysScratch_.resize(n);
  orderScratch_.resize(n);
}

void DistanceSorter::sort(Vec3 from, std::vector<uint32_t>& order) {
  const std::size_t n = count();
  order.resize(n);

  // Key: bit pattern of the squared distance. Non negative floats compare like their bits,
  // so an integer sort on them is a float sort. Inverting the bits makes the largest
  // distance the smallest key, which turns an ascending sort into back to front.
  for (std::size_t i = 0; i < n; ++i) {
    const float dx = positions_[i * 3] - from.x;
    const float dy = positions_[i * 3 + 1] - from.y;
    const float dz = positions_[i * 3 + 2] - from.z;
    const float d2 = dx * dx + dy * dy + dz * dz;
    uint32_t bits;
    std::memcpy(&bits, &d2, sizeof(bits));
    keys_[i] = ~bits;
    order[i] = static_cast<uint32_t>(i);
  }

  // LSD radix sort, 8 bits per pass, least significant digit first. Each pass is a
  // stable counting sort, so after four passes the full 32-bit key is ordered.
  uint32_t* keysIn = keys_.data();
  uint32_t* keysOut = keysScratch_.data();
  uint32_t* orderIn = order.data();
  uint32_t* orderOut = orderScratch_.data();
  for (int shift = 0; shift < 32; shift += 8) {
    std::array<uint32_t, 256> histogram{};
    for (std::size_t i = 0; i < n; ++i) ++histogram[(keysIn[i] >> shift) & 0xFF];
    uint32_t running = 0;
    for (uint32_t& h : histogram) {
      const uint32_t c = h;
      h = running;
      running += c;
    }
    for (std::size_t i = 0; i < n; ++i) {
      const uint32_t digit = (keysIn[i] >> shift) & 0xFF;
      const uint32_t dst = histogram[digit]++;
      keysOut[dst] = keysIn[i];
      orderOut[dst] = orderIn[i];
    }
    std::swap(keysIn, keysOut);
    std::swap(orderIn, orderOut);
  }
  // An even number of passes leaves the result in the caller's buffer. The copy only runs
  // if someone changes the pass count to an odd number.
  if (orderIn != order.data()) std::memcpy(order.data(), orderIn, n * sizeof(uint32_t));
}

}  // namespace splat
