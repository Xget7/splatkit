#include "splat/loading/WorldLoader.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "splat/formats/GlbDecoder.h"
#include "splat/formats/SplatDecoder.h"
#include "splat/io/MappedFile.h"
#include "splat/sorting/SpatialOrder.h"

namespace splat {
namespace {

using Clock = std::chrono::steady_clock;

double millisSince(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

}  // namespace

void WorldLoader::setBudget(int budget) {
  budget_.store(std::max(budget, 0));
}

Result<WorldLoader::WorldReport> WorldLoader::loadWorld(const std::uint8_t* data,
                                                        std::size_t size) {
  WorldReport report;
  auto start = Clock::now();
  auto decoded = decodeSplatFile(data, size);
  if (!decoded) return decoded.error();
  report.decodeMillis = millisSince(start);
  auto cloud = std::make_unique<SplatCloud>(std::move(decoded.value()));
  report.splatCount = cloud->count();
  report.shDegree = cloud->shDegree;
  report.bounds = cloud->bounds;

  start = Clock::now();
  reorderSpatially(*cloud);
  report.reorderMillis = millisSince(start);

  auto world = std::make_unique<World>();
  world->budget = budget();
  world->sourceCount = cloud->count();
  if (world->budget > 0) {
    start = Clock::now();
    world->tree = std::make_shared<const LodTree>(buildLodTree(std::move(*cloud)));
    report.nodeCount = world->tree->nodeCount();
    report.treeMillis = millisSince(start);
  } else {
    world->cloud = std::move(cloud);
  }

  const std::lock_guard<std::mutex> lock(mutex_);
  pendingWorld_ = std::move(world);
  return report;
}

Result<WorldLoader::WorldReport> WorldLoader::loadWorldFile(const std::string& path) {
  auto file = MappedFile::open(path);
  if (!file) return file.error();
  return loadWorld(file.value().data(), file.value().size());
}

Result<WorldLoader::ColliderReport> WorldLoader::loadCollider(const std::uint8_t* data,
                                                              std::size_t size) {
  const auto start = Clock::now();
  auto decoded = decodeGlb(data, size);
  if (!decoded) return decoded.error();
  auto collider = std::make_unique<Collider>(decoded.value());
  ColliderReport report;
  report.triangleCount = collider->triangleCount();
  report.millis = millisSince(start);

  const std::lock_guard<std::mutex> lock(mutex_);
  pendingCollider_ = std::move(collider);
  return report;
}

Result<WorldLoader::ColliderReport> WorldLoader::loadColliderFile(const std::string& path) {
  auto file = MappedFile::open(path);
  if (!file) return file.error();
  return loadCollider(file.value().data(), file.value().size());
}

std::unique_ptr<WorldLoader::World> WorldLoader::takeWorld() {
  const std::lock_guard<std::mutex> lock(mutex_);
  return std::move(pendingWorld_);
}

std::unique_ptr<Collider> WorldLoader::takeCollider() {
  const std::lock_guard<std::mutex> lock(mutex_);
  return std::move(pendingCollider_);
}

}  // namespace splat
