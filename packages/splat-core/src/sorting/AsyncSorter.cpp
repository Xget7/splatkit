#include "splat/sorting/AsyncSorter.h"

#include <chrono>
#include <utility>

namespace splat {

AsyncSorter::AsyncSorter(std::vector<float> positions) : sorter_(std::move(positions)) {
  thread_ = std::thread([this] { run(); });
}

AsyncSorter::~AsyncSorter() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
  }
  wake_.notify_all();
  thread_.join();
}

void AsyncSorter::request(Vec3 from) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_ = Request{from, std::nullopt};
  }
  wake_.notify_one();
}

void AsyncSorter::requestVisible(const Frustum& frustum) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_ = Request{frustum.origin, frustum};
  }
  wake_.notify_one();
}

std::optional<AsyncSorter::Result> AsyncSorter::take() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::optional<Result> out = std::move(finished_);
  finished_.reset();
  return out;
}

void AsyncSorter::run() {
  std::vector<uint32_t> order;
  for (;;) {
    Request request;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      wake_.wait(lock, [this] { return stop_ || pending_.has_value(); });
      if (stop_) return;
      request = *pending_;
      pending_.reset();
    }
    const auto start = std::chrono::steady_clock::now();
    if (request.frustum) {
      sorter_.sortVisible(*request.frustum, order);
    } else {
      sorter_.sort(request.from, order);
    }
    const double millis =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::lock_guard<std::mutex> lock(mutex_);
    // An untaken result is stale now; its buffer becomes the next sort's scratch.
    std::vector<uint32_t> recycled = finished_ ? std::move(finished_->order) : std::vector<uint32_t>();
    finished_ = Result{std::move(order), millis};
    order = std::move(recycled);
  }
}

}  // namespace splat
