#include "splat/navigation/CharacterController.h"
#include "splat/navigation/Collider.h"

#include <gtest/gtest.h>

#include <cmath>

namespace splat {
namespace {

// A room: floor 10 x 10 m at y = 0, one wall at x = 5 from y = 0 to 3.
TriangleMesh room() {
  TriangleMesh m;
  auto quad = [&](Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
    const uint32_t base = static_cast<uint32_t>(m.vertexCount());
    for (Vec3 v : {a, b, c, d}) {
      m.positions.push_back(v.x);
      m.positions.push_back(v.y);
      m.positions.push_back(v.z);
    }
    for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) m.indices.push_back(base + i);
  };
  quad({-5, 0, -5}, {5, 0, -5}, {5, 0, 5}, {-5, 0, 5});   // floor
  quad({5, 0, -5}, {5, 3, -5}, {5, 3, 5}, {5, 0, 5});     // wall at x = 5
  return m;
}

TEST(Collider, RaycastHitsFloorStraightDown) {
  Collider c(room());
  EXPECT_EQ(c.triangleCount(), 4u);
  auto hit = c.raycast({1, 1.5f, 1}, {0, -1, 0}, 4);
  ASSERT_TRUE(hit);
  EXPECT_NEAR(hit->distance, 1.5f, 1e-4f);
  EXPECT_NEAR(hit->point.y, 0, 1e-4f);
  EXPECT_NEAR(hit->normal.y, 1, 1e-4f);  // faces the ray, which points down
}

TEST(Collider, RaycastMissesOutsideMaxDistanceAndOutsideBounds) {
  Collider c(room());
  EXPECT_FALSE(c.raycast({1, 1.5f, 1}, {0, -1, 0}, 1.0f));
  EXPECT_FALSE(c.raycast({20, 1, 20}, {0, -1, 0}, 4));
  EXPECT_FALSE(c.raycast({0, 1, 0}, {0, 1, 0}, 100));  // nothing above
}

TEST(Collider, RaycastFindsNearestAcrossCells) {
  Collider c(room(), 0.5f);
  // Diagonal ray from the far corner towards the wall: it crosses many cells.
  auto hit = c.raycast({-4, 1, -4}, {1, 0, 0.3f}, 20);
  ASSERT_TRUE(hit);
  EXPECT_NEAR(hit->point.x, 5, 1e-3f);
  EXPECT_NEAR(hit->normal.x, -1, 1e-4f);
}

TEST(Collider, UnnormalisedDirectionGivesTheSameHit) {
  Collider c(room());
  auto a = c.raycast({1, 1.5f, 1}, {0, -1, 0}, 4);
  auto b = c.raycast({1, 1.5f, 1}, {0, -7, 0}, 4);
  ASSERT_TRUE(a && b);
  EXPECT_NEAR(a->distance, b->distance, 1e-6f);
}

TEST(CharacterController, WallBlocksAndSlides) {
  Collider c(room());
  CharacterController player(c);
  player.setPosition({4.0f, 1.5f, 0});
  // Walking straight into the wall stops bodyRadius short of it.
  EXPECT_TRUE(player.move({3, 0, 0}));
  EXPECT_NEAR(player.position().x, 5 - 0.35f, 1e-3f);
  // Walking diagonally into it slides along z.
  player.setPosition({4.0f, 1.5f, 0});
  EXPECT_TRUE(player.move({3, 0, 1}));
  EXPECT_NEAR(player.position().x, 5 - 0.35f, 1e-3f);
  EXPECT_GT(player.position().z, 0.5f);
}

TEST(CharacterController, RefusesToLeaveTheFloor) {
  Collider c(room());
  CharacterController player(c);
  player.setPosition({-4.5f, 1.5f, 0});
  EXPECT_FALSE(player.move({-2, 0, 0}));  // off the edge: no floor there
  EXPECT_NEAR(player.position().x, -4.5f, 1e-6f);
  EXPECT_TRUE(player.move({0, 0, 2}));
}

TEST(CharacterController, SnapsTowardEyeHeight) {
  Collider c(room());
  CharacterController player(c);
  player.setPosition({0, 3.0f, 0});
  for (int i = 0; i < 60; ++i) player.update(1.0f / 60.0f);
  EXPECT_NEAR(player.position().y, 1.5f, 0.01f);
}

}  // namespace
}  // namespace splat
