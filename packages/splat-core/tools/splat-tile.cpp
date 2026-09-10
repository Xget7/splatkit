// splat-tile: partitions a Gaussian splat scene into tiles with offline levels of detail,
// the form the engine streams (ADR 0015).
//
//   splat-tile in.ply|in.spz out_dir [--tile N] [--sh N]
//
// --tile N  the most splats per tile, 262144 by default. Leaves split until they fit and
//           every level above merges back down to it.
// --sh N    keeps spherical harmonics up to degree N before tiling.
//
// Writes out_dir/tileset.json and one spz per tile. Coordinates are written as they are.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include "load-spz.h"
#include "splat/tiles/TileBuilder.h"

namespace {

int usage() {
  std::fprintf(stderr, "usage: splat-tile in.ply|in.spz out_dir [--tile N] [--sh N]\n");
  return 2;
}

// Truncates the harmonics to `degree`; spz stores them per point, coefficient major.
void truncateSh(spz::GaussianCloud& cloud, int degree) {
  if (degree >= cloud.shDegree) return;
  const int from = (cloud.shDegree + 1) * (cloud.shDegree + 1) - 1;
  const int to = (degree + 1) * (degree + 1) - 1;
  std::vector<float> sh(static_cast<size_t>(cloud.numPoints) * to * 3);
  for (int i = 0; i < cloud.numPoints; ++i) {
    std::memcpy(&sh[static_cast<size_t>(i) * to * 3], &cloud.sh[static_cast<size_t>(i) * from * 3],
                static_cast<size_t>(to) * 3 * sizeof(float));
  }
  cloud.sh = std::move(sh);
  cloud.shDegree = degree;
}

bool endsWith(const std::string& s, const char* suffix) {
  const size_t n = std::strlen(suffix);
  return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

int run(int argc, char** argv) {
  if (argc < 3) return usage();
  const std::string in = argv[1];
  const std::string out = argv[2];
  splat::TileBuildOptions options;
  int sh = -1;
  for (int i = 3; i < argc; ++i) {
    if (std::strcmp(argv[i], "--tile") == 0 && i + 1 < argc)
      options.tileSplats = static_cast<uint32_t>(std::atoi(argv[++i]));
    else if (std::strcmp(argv[i], "--sh") == 0 && i + 1 < argc)
      sh = std::atoi(argv[++i]);
    else
      return usage();
  }
  if (sh > 3 || options.tileSplats == 0) return usage();

  const auto start = std::chrono::steady_clock::now();
  spz::GaussianCloud cloud =
      endsWith(in, ".spz") ? spz::loadSpz(in, {}) : spz::loadSplatFromPly(in, {});
  if (cloud.numPoints <= 0) {
    std::fprintf(stderr, "could not read %s as a Gaussian splat scene\n", in.c_str());
    return 1;
  }
  std::printf("%d splats, sh degree %d\n", cloud.numPoints, cloud.shDegree);
  if (sh >= 0) truncateSh(cloud, sh);
  std::filesystem::create_directories(out);

  auto built = splat::buildTiles(std::move(cloud), out, options);
  if (!built.ok()) {
    std::fprintf(stderr, "%s\n", built.error().message.c_str());
    return 1;
  }
  const splat::Tileset& set = built.value();
  std::vector<std::size_t> tilesPerLevel, splatsPerLevel;
  std::vector<std::uintmax_t> bytesPerLevel;
  for (const splat::Tile& t : set.tiles) {
    const auto level = static_cast<std::size_t>(t.level);
    if (level >= tilesPerLevel.size()) {
      tilesPerLevel.resize(level + 1);
      splatsPerLevel.resize(level + 1);
      bytesPerLevel.resize(level + 1);
    }
    ++tilesPerLevel[level];
    splatsPerLevel[level] += t.count;
    bytesPerLevel[level] += std::filesystem::file_size(std::filesystem::path(out) / t.file);
  }
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  std::printf("wrote %zu tiles in %.1f s\n", set.tiles.size(), seconds);
  for (std::size_t level = 0; level < tilesPerLevel.size(); ++level) {
    std::printf("level %zu: %zu tiles, %zu splats, %.1f MB\n", level, tilesPerLevel[level],
                splatsPerLevel[level], bytesPerLevel[level] / 1048576.0);
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "splat-tile failed: %s\n", e.what());
    return 1;
  }
}
