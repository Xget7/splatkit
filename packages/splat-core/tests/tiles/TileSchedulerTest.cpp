#include "splat/tiles/TileScheduler.h"

#include <algorithm>
#include <memory>

#include <gtest/gtest.h>

namespace splat {
namespace {

// A root cube of 20 with eight octant children of 100 splats each, their boxes pulled
// in to [1, 9] on every axis so that a camera inside one sees only that one.
std::shared_ptr<const Tileset> octants() {
  Tileset set;
  set.shDegree = 0;
  set.splatCount = 800;
  Tile root;
  root.file = "root.spz";
  root.level = 1;
  root.bounds = {{-10, -10, -10}, {10, 10, 10}};
  root.count = 100;
  root.error = 1.0f;
  for (int i = 0; i < 8; ++i) {
    Tile child;
    child.file = "child" + std::to_string(i) + ".spz";
    child.count = 100;
    for (int k = 0; k < 3; ++k) {
      const bool high = (i >> k) & 1;
      child.bounds.min[k] = high ? 1.0f : -9.0f;
      child.bounds.max[k] = high ? 9.0f : -1.0f;
    }
    root.children.push_back(static_cast<std::uint32_t>(set.tiles.size()));
    set.tiles.push_back(child);
  }
  set.root = static_cast<std::uint32_t>(set.tiles.size());
  set.tiles.push_back(root);
  return std::make_shared<const Tileset>(std::move(set));
}

TileView from(Vec3 origin, Vec3 forward, float tanHalf, float limit) {
  TileView view;
  view.frustum = Frustum::make(origin, forward, {0, 1, 0}, tanHalf, tanHalf, 0.0f);
  view.pixelScaleLimit = limit;
  return view;
}

std::vector<std::uint32_t> tilesOf(const std::vector<TileScheduler::Load>& loads) {
  std::vector<std::uint32_t> out;
  for (const auto& l : loads) out.push_back(l.tile);
  std::sort(out.begin(), out.end());
  return out;
}

TEST(TileScheduler, AsksForTheRootFirstAndDrawsItUntilTheChildrenAreThere) {
  auto set = octants();
  TileScheduler scheduler(set, 1000);
  const TileView inside = from({0, 0, 0}, {0, 0, -1}, 1.0f, 0.001f);

  auto plan = scheduler.plan(inside);
  EXPECT_TRUE(plan.draw.empty());
  ASSERT_EQ(plan.load.size(), 1u);
  EXPECT_EQ(plan.load[0].tile, set->root);
  EXPECT_EQ(scheduler.state(set->root), TileState::loading);

  scheduler.markResident(set->root);
  plan = scheduler.plan(inside);
  EXPECT_EQ(plan.draw, std::vector<std::uint32_t>{set->root});
  // The four octants in front of the camera; the four behind it are not wanted.
  EXPECT_EQ(tilesOf(plan.load), (std::vector<std::uint32_t>{0, 1, 2, 3}));
  EXPECT_EQ(scheduler.held(), 500u);

  for (int i = 0; i < 2; ++i) scheduler.markResident(static_cast<std::uint32_t>(i));
  plan = scheduler.plan(inside);
  EXPECT_EQ(plan.draw, std::vector<std::uint32_t>{set->root});
  EXPECT_EQ(tilesOf(plan.load), (std::vector<std::uint32_t>{2, 3}));

  for (int i = 2; i < 4; ++i) scheduler.markResident(static_cast<std::uint32_t>(i));
  plan = scheduler.plan(inside);
  std::sort(plan.draw.begin(), plan.draw.end());
  EXPECT_EQ(plan.draw, (std::vector<std::uint32_t>{0, 1, 2, 3}));
  EXPECT_TRUE(plan.load.empty());
}

TEST(TileScheduler, FarAwayTheRootIsFineEnough) {
  auto set = octants();
  TileScheduler scheduler(set, 1000);
  scheduler.plan(from({0, 0, 1000}, {0, 0, -1}, 1.0f, 0.01f));
  scheduler.markResident(set->root);
  auto plan = scheduler.plan(from({0, 0, 1000}, {0, 0, -1}, 1.0f, 0.01f));
  EXPECT_EQ(plan.draw, std::vector<std::uint32_t>{set->root});
  EXPECT_TRUE(plan.load.empty());
}

TEST(TileScheduler, OnlyWhatTheCameraSeesIsWanted) {
  auto set = octants();
  TileScheduler scheduler(set, 1000);
  // Inside octant 7 (all axes high), looking +x with a narrow view.
  const TileView narrow = from({5, 5, 5}, {1, 0, 0}, 0.27f, 0.001f);
  scheduler.plan(narrow);
  scheduler.markResident(set->root);
  auto plan = scheduler.plan(narrow);
  EXPECT_EQ(tilesOf(plan.load), std::vector<std::uint32_t>{7});
  scheduler.markResident(7);
  plan = scheduler.plan(narrow);
  EXPECT_EQ(plan.draw, std::vector<std::uint32_t>{7});
}

TEST(TileScheduler, MakesRoomByDroppingWhatWasNotDrawnLately) {
  auto set = octants();
  TileScheduler scheduler(set, 250);  // the root and one octant
  const TileView inSeven = from({5, 5, 5}, {1, 0, 0}, 0.27f, 0.001f);
  const TileView inZero = from({-5, -5, -5}, {-1, 0, 0}, 0.27f, 0.001f);
  scheduler.plan(inSeven);
  scheduler.markResident(set->root);
  scheduler.plan(inSeven);
  scheduler.markResident(7);
  EXPECT_EQ(scheduler.held(), 200u);

  auto plan = scheduler.plan(inZero);
  ASSERT_EQ(plan.drop.size(), 1u);
  EXPECT_EQ(plan.drop[0].tile, 7u);
  EXPECT_EQ(tilesOf(plan.load), std::vector<std::uint32_t>{0});
  EXPECT_EQ(scheduler.state(7), TileState::absent);
  EXPECT_EQ(scheduler.held(), 200u);
}

TEST(TileScheduler, WhatIsDrawnThisFrameIsNeverDropped) {
  auto set = octants();
  TileScheduler scheduler(set, 250);
  const TileView inside = from({0, 0, 0}, {0, 0, -1}, 1.0f, 0.001f);
  scheduler.plan(inside);
  scheduler.markResident(set->root);
  auto plan = scheduler.plan(inside);
  // Eight octants wanted, one fits next to the root; the root stays.
  EXPECT_EQ(plan.load.size(), 1u);
  EXPECT_TRUE(plan.drop.empty());
  EXPECT_EQ(scheduler.state(set->root), TileState::resident);
}

TEST(TileScheduler, AFailedTileIsNeverAskedForAgain) {
  auto set = octants();
  TileScheduler scheduler(set, 1000);
  const TileView narrow = from({5, 5, 5}, {1, 0, 0}, 0.27f, 0.001f);
  scheduler.plan(narrow);
  scheduler.markResident(set->root);
  scheduler.plan(narrow);
  scheduler.markFailed(7);
  auto plan = scheduler.plan(narrow);
  EXPECT_TRUE(plan.load.empty());
  EXPECT_EQ(plan.draw, std::vector<std::uint32_t>{set->root});
  EXPECT_EQ(scheduler.held(), 100u);
}

TEST(TileScheduler, AnAbandonedLoadFreesItsRange) {
  auto set = octants();
  TileScheduler scheduler(set, 1000);
  const TileView narrow = from({5, 5, 5}, {1, 0, 0}, 0.27f, 0.001f);
  scheduler.plan(narrow);
  scheduler.markResident(set->root);
  scheduler.plan(narrow);
  EXPECT_EQ(scheduler.held(), 200u);
  scheduler.markAbsent(7);
  EXPECT_EQ(scheduler.held(), 100u);
  auto plan = scheduler.plan(narrow);
  EXPECT_EQ(tilesOf(plan.load), std::vector<std::uint32_t>{7});
}

}  // namespace
}  // namespace splat
