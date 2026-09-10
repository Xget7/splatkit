#pragma once

#include <cstdint>
#include <string>

#include "splat/core/Result.h"
#include "splat/tiles/Tileset.h"

namespace spz {
struct GaussianCloud;
}

namespace splat {

struct TileBuildOptions {
  // The most splats a tile holds, at any level. Leaves split until they fit; each level
  // above merges the tiles below it back down to this many.
  std::uint32_t tileSplats = 262144;
};

// Partitions a cloud into an octree of tiles and builds every level above the leaves by
// merging, offline (ADR 0015). Writes one spz file per tile and `tileset.json` into
// `directory`, which must exist. The cloud is consumed. Its coordinates are written as
// they are, so a tiled world stands in the frame of the file it came from.
Result<Tileset> buildTiles(const spz::GaussianCloud& cloud, const std::string& directory,
                           const TileBuildOptions& options = {});

}  // namespace splat
