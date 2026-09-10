#include "splat/tiles/TileStreamer.h"

#include <utility>

namespace splat {
namespace {

SplatDecodeOptions decodeOptions(const TiledWorld& world) {
  SplatDecodeOptions options;
  options.sourceFrame = world.sourceFrame;
  return options;
}

}  // namespace

TileStreamer::TileStreamer(TiledWorld world, const StreamOptions& options)
    : world_(std::move(world)),
      scheduler_(world_.tileset, options.residency),
      loader_(decodeOptions(world_), options.loaderThreads),
      sorter_(options.residency) {}

TileStreamer::Step TileStreamer::update(const TileView& view) {
  Step step;
  for (TileLoader::Loaded& loaded : loader_.take()) {
    if (scheduler_.state(loaded.tile) != TileState::loading) continue;  // dropped meanwhile
    if (!loaded.cloud) {
      scheduler_.markFailed(loaded.tile);
      step.failed.push_back(loaded.tile);
      continue;
    }
    arrived_[loaded.tile] = std::make_unique<SplatCloud>(std::move(loaded.cloud.value()));
  }

  TileScheduler::Plan plan = scheduler_.plan(view);
  std::vector<TileLoader::Request> queue;
  for (const TileScheduler::Load& load : plan.load) {
    if (arrived_.count(load.tile)) continue;  // read already, waiting for its upload
    queue.push_back({load.tile, world_.tilePath(load.tile), load.priority});
  }
  for (const std::uint32_t tile : loader_.setQueue(std::move(queue))) scheduler_.markAbsent(tile);

  for (const auto& [tile, cloud] : arrived_) {
    step.arrived.push_back({tile, scheduler_.offset(tile), cloud.get()});
  }
  step.loading = loader_.pending();

  if (plan.draw != drawn_) {
    drawn_ = std::move(plan.draw);
    ranges_.clear();
    drawnSplats_ = 0;
    for (const std::uint32_t tile : drawn_) {
      const std::uint32_t count = world_.tileset->tiles[tile].count;
      ranges_.push_back({scheduler_.offset(tile), count});
      drawnSplats_ += count;
    }
    step.drawChanged = true;
  }
  return step;
}

void TileStreamer::commit(std::uint32_t tile) {
  auto it = arrived_.find(tile);
  if (it == arrived_.end()) return;
  sorter_.place(scheduler_.offset(tile), std::move(it->second->positions));
  arrived_.erase(it);
  scheduler_.markResident(tile);
}

void TileStreamer::fail(std::uint32_t tile) {
  arrived_.erase(tile);
  scheduler_.markAbsent(tile);
}

void TileStreamer::requestVisible(const Frustum& frustum) {
  sorter_.requestVisible(frustum, ranges_);
}

std::optional<SlabSorter::Result> TileStreamer::take() {
  return sorter_.take();
}

}  // namespace splat
