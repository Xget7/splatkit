#include "splat/tiles/TileScheduler.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace splat {

TileScheduler::TileScheduler(std::shared_ptr<const Tileset> tileset, std::uint32_t residency)
    : tileset_(std::move(tileset)),
      slab_(residency),
      states_(tileset_->tiles.size(), TileState::absent),
      offsets_(tileset_->tiles.size(), 0),
      lastUsed_(tileset_->tiles.size(), 0) {}

bool TileScheduler::visible(std::uint32_t index, const TileView& view) const {
  const Tile& tile = tileset_->tiles[index];
  return view.frustum.intersects(tile.bounds.min, tile.bounds.max);
}

// The world units per unit depth the tile hides: its error over the distance from the
// camera to its box, unbounded with the camera inside.
float TileScheduler::screenError(std::uint32_t index, Vec3 origin) const {
  const Tile& tile = tileset_->tiles[index];
  float d2 = 0.0f;
  for (int k = 0; k < 3; ++k) {
    const float gap = std::max({tile.bounds.min[k] - origin[k], origin[k] - tile.bounds.max[k], 0.0f});
    d2 += gap * gap;
  }
  if (d2 <= 0.0f) return std::numeric_limits<float>::infinity();
  return tile.error / std::sqrt(d2);
}

bool TileScheduler::fineEnough(std::uint32_t index, const TileView& view) const {
  const Tile& tile = tileset_->tiles[index];
  if (tile.level == 0 || tile.children.empty()) return true;
  return screenError(index, view.frustum.origin) <= view.pixelScaleLimit;
}

void TileScheduler::want(std::uint32_t index, float priority, std::vector<Wanted>& wanted) {
  if (states_[index] == TileState::failed) return;
  lastUsed_[index] = frame_;
  wanted.push_back({index, priority});
}

void TileScheduler::visit(std::uint32_t index, const TileView& view, Plan& plan,
                          std::vector<Wanted>& wanted) {
  if (!visible(index, view)) return;
  const Tile& tile = tileset_->tiles[index];
  const bool fine = fineEnough(index, view);
  const float error = screenError(index, view.frustum.origin);

  std::vector<std::uint32_t> shown;  // the visible children
  bool allResident = true;
  for (const std::uint32_t child : tile.children) {
    if (!visible(child, view)) continue;
    shown.push_back(child);
    if (states_[child] != TileState::resident) allResident = false;
  }

  if (states_[index] == TileState::resident) {
    lastUsed_[index] = frame_;
    if (fine) {
      plan.draw.push_back(index);
      return;
    }
    if (allResident) {
      for (const std::uint32_t child : shown) visit(child, view, plan, wanted);
      return;
    }
    // Drawn as it is while the finer cover arrives.
    plan.draw.push_back(index);
    for (const std::uint32_t child : shown) {
      if (states_[child] != TileState::resident) want(child, error, wanted);
    }
    return;
  }

  // Not resident. Wanted unless its children already cover it; the root always is, as
  // the cover a turn falls back on.
  const bool root = index == tileset_->root;
  if (root || fine || !allResident) {
    want(index, root ? std::numeric_limits<float>::infinity() : error, wanted);
  }
  // Whatever finer cover is there is drawn meanwhile; the rest is a hole until it lands.
  for (const std::uint32_t child : shown) {
    if (states_[child] == TileState::resident) visit(child, view, plan, wanted);
  }
}

// Reserves a slab range for the tile, evicting the least recently used tiles not touched
// by this plan until it fits. False when it cannot fit even then.
bool TileScheduler::place(std::uint32_t index, Plan& plan, std::vector<std::uint32_t>& evictable) {
  const std::uint32_t count = tileset_->tiles[index].count;
  if (count > slab_.capacity()) {
    states_[index] = TileState::failed;
    return false;
  }
  for (;;) {
    if (auto offset = slab_.allocate(count)) {
      offsets_[index] = *offset;
      states_[index] = TileState::loading;
      return true;
    }
    if (evictable.empty()) return false;
    const std::uint32_t victim = evictable.back();
    evictable.pop_back();
    plan.drop.push_back({victim, offsets_[victim], tileset_->tiles[victim].count});
    release(victim, TileState::absent);
  }
}

void TileScheduler::release(std::uint32_t tile, TileState next) {
  if (states_[tile] == TileState::loading || states_[tile] == TileState::resident) {
    slab_.release(offsets_[tile], tileset_->tiles[tile].count);
  }
  states_[tile] = next;
}

TileScheduler::Plan TileScheduler::plan(const TileView& view) {
  ++frame_;
  Plan plan;
  std::vector<Wanted> wanted;
  visit(tileset_->root, view, plan, wanted);

  std::stable_sort(wanted.begin(), wanted.end(),
                   [](const Wanted& a, const Wanted& b) { return a.priority > b.priority; });

  // Eviction candidates, most recently used last so the back is the least recent.
  std::vector<std::uint32_t> evictable;
  for (std::uint32_t i = 0; i < states_.size(); ++i) {
    if (states_[i] == TileState::resident && lastUsed_[i] != frame_) evictable.push_back(i);
  }
  std::sort(evictable.begin(), evictable.end(),
            [&](std::uint32_t a, std::uint32_t b) { return lastUsed_[a] > lastUsed_[b]; });

  for (const Wanted& w : wanted) {
    if (states_[w.tile] == TileState::absent && !place(w.tile, plan, evictable)) continue;
    if (states_[w.tile] != TileState::loading) continue;
    plan.load.push_back({w.tile, offsets_[w.tile], w.priority});
  }
  return plan;
}

void TileScheduler::markResident(std::uint32_t tile) {
  if (states_[tile] == TileState::loading) states_[tile] = TileState::resident;
}

void TileScheduler::markAbsent(std::uint32_t tile) {
  release(tile, TileState::absent);
}

void TileScheduler::markFailed(std::uint32_t tile) {
  release(tile, TileState::failed);
}

}  // namespace splat
