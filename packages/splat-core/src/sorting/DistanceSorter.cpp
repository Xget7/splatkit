#include "splat/sorting/DistanceSorter.h"

#include <array>
#include <cstring>
#include <thread>
#include <utility>
#include <vector>

namespace splat {

DistanceSorter::DistanceSorter(std::vector<float> positions) : positions_(std::move(positions)) {
  const std::size_t n = count();
  keys_.resize(n);
  keysScratch_.resize(n);
  orderScratch_.resize(n);
}

namespace {

// Worker threads for the cull pass; small clouds stay on one.
constexpr std::size_t kMaxWorkers = 4;
constexpr std::size_t kMinPerWorker = 100000;

// Key: bit pattern of the squared distance. Non negative floats compare like their bits,
// so an integer sort on them is a float sort. Inverting the bits makes the largest
// distance the smallest key, which turns an ascending sort into back to front.
inline uint32_t distanceKey(const float* p, Vec3 from) {
  const float dx = p[0] - from.x;
  const float dy = p[1] - from.y;
  const float dz = p[2] - from.z;
  const float d2 = dx * dx + dy * dy + dz * dz;
  uint32_t bits;
  std::memcpy(&bits, &d2, sizeof(bits));
  return ~bits;
}

}  // namespace

void DistanceSorter::sort(Vec3 from, std::vector<uint32_t>& order) {
  const std::size_t n = count();
  order.resize(n);
  for (std::size_t i = 0; i < n; ++i) {
    keys_[i] = distanceKey(&positions_[i * 3], from);
    order[i] = static_cast<uint32_t>(i);
  }
  radixSort(n, order);
}

std::size_t DistanceSorter::sortVisible(const Frustum& frustum, std::vector<uint32_t>& order) {
  const std::size_t n = count();
  order.resize(n);

  // The cull and key pass is the bulk of the work at 2M splats and is embarrassingly
  // parallel: each worker compacts its slice into scratch, then the slices are joined.
  const std::size_t workers = std::min<std::size_t>(kMaxWorkers, std::max<std::size_t>(1, n / kMinPerWorker));
  const std::size_t slice = (n + workers - 1) / workers;
  std::vector<std::size_t> counts(workers, 0);
  auto cullSlice = [&](std::size_t w) {
    const std::size_t begin = w * slice;
    const std::size_t end = std::min(n, begin + slice);
    std::size_t out = begin;
    for (std::size_t i = begin; i < end; ++i) {
      const float* p = &positions_[i * 3];
      if (!frustum.contains({p[0], p[1], p[2]})) continue;
      keysScratch_[out] = distanceKey(p, frustum.origin);
      orderScratch_[out] = static_cast<uint32_t>(i);
      ++out;
    }
    counts[w] = out - begin;
  };
  std::vector<std::thread> threads;
  for (std::size_t w = 1; w < workers; ++w) threads.emplace_back(cullSlice, w);
  cullSlice(0);
  for (auto& t : threads) t.join();

  std::size_t visible = 0;
  for (std::size_t w = 0; w < workers; ++w) {
    const std::size_t begin = w * slice;
    std::memcpy(&keys_[visible], &keysScratch_[begin], counts[w] * sizeof(uint32_t));
    std::memcpy(&order[visible], &orderScratch_[begin], counts[w] * sizeof(uint32_t));
    visible += counts[w];
  }
  order.resize(visible);
  radixSort(visible, order);
  return visible;
}

void DistanceSorter::radixSort(std::size_t n, std::vector<uint32_t>& order) {
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
