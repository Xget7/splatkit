#include "splat/sorting/AsyncSorter.h"

#include <chrono>
#include <utility>

namespace splat {

AsyncSorter::AsyncSorter(std::vector<float> positions) : sorter_(std::move(positions)) {
  thread_ = std::thread([this] { run(); });
}

AsyncSorter::AsyncSorter(std::shared_ptr<const LodTree> tree)
    : sorter_(tree->nodes.positions), tree_(std::move(tree)) {
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
    pending_ = Request{from, std::nullopt, LodSettings{}};
  }
  wake_.notify_one();
}

void AsyncSorter::requestVisible(const Frustum& frustum, LodSettings lod) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_ = Request{frustum.origin, frustum, lod};
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
    using Clock = std::chrono::steady_clock;
    auto millisBetween = [](Clock::time_point a, Clock::time_point b) {
      return std::chrono::duration<double, std::milli>(b - a).count();
    };
    const auto start = Clock::now();
    const bool useLod = tree_ && request.lod.budget > 0;
    const bool lodChanged = request.lod.budget != sortedLod_.budget ||
                            request.lod.pixelScaleLimit != sortedLod_.pixelScaleLimit;
    const bool moved = !sortedFrom_ || sortedFrom_->x != request.from.x ||
                       sortedFrom_->y != request.from.y || sortedFrom_->z != request.from.z;
    const bool turned = useLod && dot(normalize(request.lod.view.forward), sortedForward_) < request.lod.reselectCosine;
    if (moved || lodChanged || turned) {
      if (useLod) {
        selectLodNodes(*tree_, request.from, request.lod.view, request.lod.budget, request.lod.pixelScaleLimit,
                       fullOrder_);
        sortedForward_ = normalize(request.lod.view.forward);
        lastSelectMillis_ = millisBetween(start, Clock::now());
        lastSelected_ = fullOrder_.size();
        const auto sortStart = Clock::now();
        sorter_.sortSubset(request.from, fullOrder_);
        lastSortMillis_ = millisBetween(sortStart, Clock::now());
      } else {
        sorter_.sort(request.from, fullOrder_);
        lastSortMillis_ = millisBetween(start, Clock::now());
        lastSelectMillis_ = 0;
        lastSelected_ = fullOrder_.size();
      }
      sortedFrom_ = request.from;
      sortedLod_ = request.lod;
    }
    const auto sorted = Clock::now();
    if (request.frustum) {
      sorter_.cull(fullOrder_, *request.frustum, order);
    } else {
      order = fullOrder_;
    }
    const auto culled = Clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    // An untaken result is stale now; its buffer becomes the next result's scratch.
    std::vector<uint32_t> recycled = finished_ ? std::move(finished_->order) : std::vector<uint32_t>();
    finished_ = Result{std::move(order), lastSortMillis_, millisBetween(sorted, culled), lastSelectMillis_, lastSelected_};
    order = std::move(recycled);
  }
}

}  // namespace splat
