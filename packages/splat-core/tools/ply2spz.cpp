// ply2spz: converts a Gaussian splat PLY (the 3DGS reference layout, what SuperSplat,
// Polycam and the Mip-NeRF 360 scenes export) into an SPZ container the engine loads.
//
//   ply2spz in.ply out.spz [--sh N] [--keep N]
//
// --sh N    keeps spherical harmonics up to degree N (0 to 3); the file's degree by default.
//           Degree 3 costs 92 bytes per splat on the GPU, so drop it for scenes above 2M.
// --keep N  keeps every Nth splat, for scenes too big for a phone.
//
// The PLY's coordinates are written as they are, and the reference 3DGS frame is what the
// engine assumes for a file without a frame tag, so a scene converted here stands upright.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "load-spz.h"

namespace {

int usage() {
  std::fprintf(stderr, "usage: ply2spz in.ply out.spz [--sh N] [--keep N]\n");
  return 2;
}

// Keeps every `stride`th splat of `cloud`, in place.
void thin(spz::GaussianCloud& cloud, int stride) {
  const int n = cloud.numPoints;
  const int shPerPoint = cloud.numPoints > 0 ? static_cast<int>(cloud.sh.size()) / n : 0;
  int kept = 0;
  for (int i = 0; i < n; i += stride, ++kept) {
    std::memmove(&cloud.positions[kept * 3], &cloud.positions[i * 3], 3 * sizeof(float));
    std::memmove(&cloud.scales[kept * 3], &cloud.scales[i * 3], 3 * sizeof(float));
    std::memmove(&cloud.rotations[kept * 4], &cloud.rotations[i * 4], 4 * sizeof(float));
    std::memmove(&cloud.colors[kept * 3], &cloud.colors[i * 3], 3 * sizeof(float));
    cloud.alphas[kept] = cloud.alphas[i];
    if (shPerPoint > 0) {
      std::memmove(&cloud.sh[kept * shPerPoint], &cloud.sh[i * shPerPoint], shPerPoint * sizeof(float));
    }
  }
  cloud.numPoints = kept;
  cloud.positions.resize(kept * 3);
  cloud.scales.resize(kept * 3);
  cloud.rotations.resize(kept * 4);
  cloud.colors.resize(kept * 3);
  cloud.alphas.resize(kept);
  cloud.sh.resize(kept * shPerPoint);
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

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) return usage();
  const std::string in = argv[1];
  const std::string out = argv[2];
  int sh = -1;
  int keep = 1;
  for (int i = 3; i < argc; ++i) {
    if (std::strcmp(argv[i], "--sh") == 0 && i + 1 < argc) sh = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--keep") == 0 && i + 1 < argc) keep = std::atoi(argv[++i]);
    else return usage();
  }
  if (sh > 3 || keep < 1) return usage();

  spz::GaussianCloud cloud = spz::loadSplatFromPly(in, {});
  if (cloud.numPoints <= 0) {
    std::fprintf(stderr, "could not read %s as a Gaussian splat PLY\n", in.c_str());
    return 1;
  }
  std::printf("%d splats, sh degree %d\n", cloud.numPoints, cloud.shDegree);
  if (keep > 1) thin(cloud, keep);
  if (sh >= 0) truncateSh(cloud, sh);

  std::vector<uint8_t> bytes;
  spz::PackOptions pack;
  pack.version = 2;  // gzip container, the one every reader supports
  if (!spz::saveSpz(cloud, pack, &bytes)) {
    std::fprintf(stderr, "could not pack the splats\n");
    return 1;
  }
  FILE* f = std::fopen(out.c_str(), "wb");
  if (f == nullptr || std::fwrite(bytes.data(), 1, bytes.size(), f) != bytes.size()) {
    std::fprintf(stderr, "could not write %s\n", out.c_str());
    return 1;
  }
  std::fclose(f);
  std::printf("wrote %s: %d splats, sh degree %d, %.1f MB\n", out.c_str(), cloud.numPoints, cloud.shDegree,
              bytes.size() / 1048576.0);
  return 0;
}
