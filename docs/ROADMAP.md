# Roadmap

SplatKit is experimental.
This page lists what works, what is next, and what is open for anyone to pick up.
Items marked "help wanted" have a defined scope and no owner; open an issue before starting so nobody duplicates the work.

## Works today

- `splat-core`: SPZ v2 to v4 decoding into the internal RUB frame, GLB collider decoding, radix distance sort on a background thread, uniform grid raycasts, character controller.
  Unit and integration tests, CI with ThreadSanitizer and AddressSanitizer.
- `splatkit-android`: Vulkan 1.1 renderer (swapchain, two frames in flight, validation layers in debug builds), instanced splat pipeline with back to front blending, walk and fly camera, touch, joystick and gyroscope input, `SplatSurfaceView`, `SplatHudView`.
- `apps/android-dev`: loads a World Labs kitchen and its collider, or any world pushed to its files dir, shows GPU, frame time, sort time and splat count.
- `scripts/generate_world.py`: photos of a place to a walkable world through the World Labs API, downloading the SPZ and the collider.
- The engine draws only when the camera, the sort order, the world or the surface changed; a still scene costs no GPU time.

Measured on a Xiaomi Mi 9 (Adreno 640, Vulkan 1.1.128), release build, 500k splats at 1080x2261, reproducible benchmark (`--ez benchmark true`):

| Render scale | Splat record | GPU ms mean | GPU ms p50 |
|---|---|---|---|
| 1.0 | 48 bytes | 29 to 31 | 27 to 29 |
| 1.0 | 32 bytes (current) | 25.2 | 23.0 |
| 0.7 | 48 bytes | 18.7 | 16.7 |
| 0.5 | 48 bytes | 15.0 | 13.7 |

Decode 192 ms, spatial reorder 120 ms, upload 50 ms, sort 11.5 ms on a background thread.

The 2M splat World Labs house (outdoor, bounds 50 m) on the same device, render scale 1.0:

| Splat memory order | GPU ms mean | GPU ms p50 | GPU ms p95 |
|---|---|---|---|
| As decoded | 144 | 121 | 279 |
| Morton order | 48.4 | 44.9 | 77.1 |

The vertex fetch is a random gather through the sort order, so it is bound by memory latency, not bandwidth: a fetch only shader took 160 ms per frame on the house as decoded.
Reordering the cloud along a Morton curve after decode (`splat::reorderSpatially`, 620 ms for 2M on the CPU) makes consecutive entries of the distance order hit the same cache lines and tripled the frame rate; the kitchen was already coherent and did not change.
At render scale 0.5 the house is 41 ms, so 2M splats are now vertex bound on Adreno 640: the next lever is fewer vertex fetches per splat, not fewer pixels.
Decode 595 ms, upload 300 ms, sort 44 ms for 2M.
Vertex fetch was an 8 ms floor at 48 bytes per splat; the 32 byte record (half float covariance, 8 bit colour and alpha, both lossless against SPZ) took 4 to 6 ms off every frame.
Vertex math costs nothing; blended fragments are the rest.
A compute prepass measured 4 ms slower on this GPU and was removed.
The GPU throttles above 60 C, so every number here was taken after cooling below 48 C.
Mali has not been tested.

## Towards 0.1.0 alpha

Owned by the maintainer unless stated otherwise.

1. Device numbers: decode, upload, sort and frame time for 500k, 1M and 2M splats on Adreno 640, published in the README.
2. Scene domain: a load state machine (idle, decoding, uploading, ready, failed) with error values the view can show, instead of logs.
3. Maven Central publication of `com.splatkit:splatkit-android` with the arm64 native library inside, and an Android CI job that builds the AAR on every pull request.
4. Library README with a ten line integration.

## Open for contribution

Each item says what it touches and how to prove it works.

- **Spherical harmonics in the shader** (help wanted).
  The decoder already keeps degree 1 to 3 coefficients in `SplatCloud::sh`; the vertex shader uses only the base colour.
  Touches `SplatPipeline` (a third storage buffer or a wider `GpuSplat`) and `splat.vert` (evaluate SH along the view direction).
  Proof: a PLY with degree 3 exported from the reference 3DGS code renders view dependent highlights that match the reference viewer.
- **PLY and .splat input** (help wanted).
  A `decodePly` and `decodeSplat` in `splat-core/formats` returning the same `SplatCloud`, with the RDF to RUB conversion the SPZ decoder does.
  Proof: unit tests with hand built files, and the same scene loaded from PLY and SPZ sorting and rendering the same.
- **SOG input** (help wanted).
  SOG (Spatially Ordered Gaussians, from PlayCanvas SuperSplat) stores a scene as a `meta.json` plus WebP images, about a tenth of the size of a PLY, and it is what the fastest Android viewers load.
  A `decodeSog` in `splat-core/formats` returning the same `SplatCloud` needs a WebP decoder in core (libwebp, pinned like spz) and the lookup table decode for scales, colours and rotations.
  Proof: a scene exported from SuperSplat as SOG and as PLY renders the same, plus decode time on a device.
- **GPU sort** (help wanted, needs a real device).
  A compute radix or bitonic sort producing the order buffer on the GPU, gated behind a feature flag so the CPU sort stays the baseline.
  Proof: identical order to `DistanceSorter` in a test, and frame time on Adreno and Mali with 2M splats.
- **Fewer blended fragments** (help wanted, needs a real device).
  Fragments are the cost on Adreno 640: half the pixels halve the GPU time.
  Ideas with a measurable claim: tighter quad bounds from the projected ellipse, opacity aware culling of splats that cannot change a pixel, a depth aware early out.
  A compute prepass was measured 4 ms slower here; do not resubmit it without a number from a device.
  Also measured and rejected on the Mi 9 (house p50, base 44.9 ms): one triangle per splat instead of a quad 48.1 ms, a triangle only for splats under 1.5 px 48.9 ms, and the draw split into 256k instance chunks 181 ms.
- **Mali validation** (help wanted, needs a Samsung or Pixel).
  Run the dev app, report validation messages, driver behaviour and frame times.
  A crash or a black screen with logs attached is a valuable report.
- **Swapchain pre-rotation**.
  The swapchain forces the identity transform, so the compositor rotates every frame on devices whose panel is not in the identity orientation.
  Proof: no `VK_SUBOPTIMAL_KHR` after a rotation and the same image.
- **Chunked world upload off the render thread**.
  `Engine::uploadPendingWorld` packs, uploads and waits inside `render`, which freezes the frame and needs about three times the world size at the peak.
- **Frustum cull margin derived from the projected extent**, so large splats near the edge do not pop.
- **One fetch per splat instead of four** (help wanted, needs a real device).
  Each of the four quad vertices reads the record; at 2M splats the fetch is the whole frame.
  Candidates with a number to beat (45 ms p50 on the house): a geometry-free path that reads the record once per instance through a per instance vertex attribute with `VK_VERTEX_INPUT_RATE_INSTANCE` and an index buffer, or a compute prepass that writes only screen quads for visible splats (the earlier prepass was slower on the kitchen; the house may differ).
- **Faster spatial reorder**.
  `reorderSpatially` is a `std::sort` on 64 bit keys, 620 ms for 2M splats; a radix sort or running it on the loader thread in parallel with the collider decode would hide it.
- **Smaller splat record** (help wanted, needs a real device).
  The record is 32 bytes with a spare word; SPZ stores positions with 24 bit fixed point, so 24 bytes is possible.
  Proof: the same image, and GPU ms from the benchmark against the 32 byte record.
- **Pipeline cache persisted to disk** to cut cold start time.
- **Offscreen render test** that draws three known splats and checks pixels, and a long stress run alternating loads, rotations and surface losses.

## Not planned

- iOS, macOS or visionOS inside this engine.
  The shared core is written so that a Metal engine can sit next to this one, but that is a separate package.
- React Native inside the engine.
  The React Native package wraps `SplatSurfaceView` and lives in `packages/react-native-splat`.
