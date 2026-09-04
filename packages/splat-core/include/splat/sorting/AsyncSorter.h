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

// Runs DistanceSorter on its own thread. The renderer asks for a sort whenever the
// camera has moved and keeps drawing with the last order it received; requests made
// while a sort is running collapse into one, always with the latest camera position.
class AsyncSorter {
 public:
  struct Result {
    std::vector<uint32_t> order;  // only the visible splats when a frustum was given
    double millis = 0;
  };

  explicit AsyncSorter(std::vector<float> positions);
  ~AsyncSorter();

  AsyncSorter(const AsyncSorter&) = delete;
  AsyncSorter& operator=(const AsyncSorter&) = delete;

  // Schedules a sort from this position. Cheap; call every frame the camera moved.
  void request(Vec3 from);
  // Schedules a sort of the splats inside the frustum only, from its origin.
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
};

}  // namespace splat
