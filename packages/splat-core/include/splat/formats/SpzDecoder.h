#pragma once

#include <cstddef>
#include <cstdint>

#include "splat/core/CoordinateFrame.h"
#include "splat/core/Result.h"
#include "splat/formats/SplatCloud.h"

namespace splat {

struct SpzDecodeOptions {
  // Frame the file was written in. World Labs does not tag it, so the caller declares it.
  CoordinateFrame sourceFrame = kWorldLabsFrame;
  // Largest decompressed payload accepted, read from the container before inflating.
  // Guards against decompression bombs when files come from the network. The default
  // fits 2M splats with SH degree 3 (about 128 MB) with room to spare.
  std::size_t maxDecodedBytes = 256u * 1024u * 1024u;
};

// Decodes an SPZ container (v1 to v4; gzip or zstd) into a SplatCloud in the internal frame.
// Returns `unsupportedFormat` when the bytes are not an SPZ container and
// `corrupt` when the container is recognised but cannot be decoded, is truncated or
// declares more than `maxDecodedBytes`.
Result<SplatCloud> decodeSpz(const std::uint8_t* data, std::size_t size,
                             const SpzDecodeOptions& options = {});

}  // namespace splat
