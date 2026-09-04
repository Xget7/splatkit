#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "splat/math/Mat4.h"
#include "splat/sorting/DistanceSorter.h"

namespace splat {

// Runs DistanceSorter on its own thread. The renderer asks whenever the camera moved or
// turned and keeps drawing with the last order it received; requests made while one is
// running collapse into one, always with the latest camera. A distance order does not
// depend on where the camera looks, so the thread sorts only when the origin changed and
// otherwise just culls the order it has: turning costs milliseconds, not a sort.
class AsyncSorter {
 public:
  struct Result {
    std::vector<uint32_t> order;  // only the visible splats when a frustum was given
    double sortMillis = 0;        // 0 when the previous order was reused
    double cullMillis = 0;
  };

  explicit AsyncSorter(std::vector<float> positions);
  ~AsyncSorter();

  AsyncSorter(const AsyncSorter&) = delete;
  AsyncSorter& operator=(const AsyncSorter&) = delete;

  // Schedules a full order from this position, nothing culled.
  void request(Vec3 from);
  // Schedules the visible order for this camera: a sort if its origin moved, then a cull.
  void requestVisible(const Frustum& frustum);

  // The newest finished order not yet taken, if any. Moves it out.
  std::optional<Result> take();

 private:
  void run();

  DistanceSorter sorter_;
  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable wake_;
  bool stop_ = false;
  struct Request {
    Vec3 from;
    std::optional<Frustum> frustum;
  };
  std::optional<Request> pending_;
  std::optional<Result> finished_;
  // Worker thread only.
  std::vector<uint32_t> fullOrder_;
  std::optional<Vec3> sortedFrom_;
};

}  // namespace splat
