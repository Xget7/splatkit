# Roadmap

SplatKit is experimental.
This page lists what works, what is next, and what is open for anyone to pick up.
Items marked "help wanted" have a defined scope and no owner; open an issue before starting so nobody duplicates the work.

## Works today

- `splat-core`: one decode entry point (`decodeSplatFile`, format detected from the bytes), SPZ v2 to v4 decoding into the internal RUB frame with spherical harmonics up to degree 3, GLB collider decoding, radix distance sort on a background thread, uniform grid raycasts, character controller.
  Unit and integration tests, CI with ThreadSanitizer and AddressSanitizer.
- `splatkit-android`: Vulkan 1.1 renderer (swapchain, two frames in flight, validation layers in debug builds), instanced splat pipeline with back to front blending and view dependent colour from spherical harmonics (one pipeline per degree, `maxShDegree` to trade it for memory), walk and fly camera, touch, joystick and gyroscope input, `SplatSurfaceView`, `SplatHudView`.
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

| Change | Render scale | GPU ms mean | GPU ms p50 | GPU ms p95 |
|---|---|---|---|---|
| As decoded | 1.0 | 144 | 121 | 279 |
| Morton order | 1.0 | 48.4 | 44.9 | 77.1 |
| Frustum culled in the sort (current) | 1.0 | 34.7 | 34.2 | 43.2 |
| Frustum culled in the sort (current) | 0.7 | 18.1 | 17.5 | 24.4 |
| Frustum culled in the sort (current) | 0.5 | 13.9 | 13.7 | 18.2 |

The vertex fetch is a random gather through the sort order, so it is bound by memory latency, not bandwidth: a fetch only shader took 160 ms per frame on the house as decoded.
Reordering the cloud along a Morton curve after decode (`splat::reorderSpatially`, 620 ms for 2M on the CPU) makes consecutive entries of the distance order hit the same cache lines and tripled the frame rate; the kitchen was already coherent and did not change.
After that the frame was bound per splat processed, not per pixel: render scale 0.5 still took 41 ms, and a compute pass that copied the records into draw order so the vertex shader reads sequentially changed nothing (46.3 against 46.9 ms p50).
Only the splats inside a widened frustum reach the GPU, so it sees 170k to 290k of the 2M splats during a turn: 32 ms at full resolution, 60 fps at render scale 0.5.
The distance order does not depend on where the camera looks, so turning never sorts: the sorter thread keeps the full order (44 ms, only when the camera moves) and turning runs a cull of it (10 ms for 2M on four threads, two streaming passes through a visibility bitmap).
The cull margin is 10 degrees plus the turn rate times 50 ms, so a flick of the phone finds its edges already drawn; the first version re-sorted the culled set with a fixed margin and showed empty edges on fast turns.
Spherical harmonics cost nothing measurable on the raccoon sample (932k splats, degree 3, 92 bytes per splat extra): p50 12.5 ms with them, 12.4 without; decode 634 ms, reorder 216 ms, upload 501 ms in release.
Decode 530 ms, spatial reorder 470 ms, upload 270 ms for 2M.
Vertex fetch was an 8 ms floor at 48 bytes per splat; the 32 byte record (half float covariance, 8 bit colour and alpha, both lossless against SPZ) took 4 to 6 ms off every frame.
Vertex math costs nothing; blended fragments are the rest.
A compute prepass measured 4 ms slower on this GPU and was removed.
The GPU throttles above 60 C, so every number here was taken after cooling below 48 C.
Mali has not been tested.

## Towards 0.1.0 alpha

Owned by the maintainer unless stated otherwise.

1. Device numbers: decode, upload, sort and frame time for 500k, 1M and 2M splats on Adreno 640, published in the README.
2. Done: `SplatSurfaceView.Listener` reports world and collider outcomes with messages, and `isAvailable` says whether Vulkan started.
3. Maven Central publication of `com.splatkit:splatkit-android` with the arm64 native library inside.
   The Gradle side is done (`./gradlew :splatkit:publishToMavenCentral` in `apps/android-dev`, credentials and signing key from the environment as the library's `build.gradle.kts` documents); what remains is the Sonatype namespace for `com.splatkit` and the first upload.
   Done: the Android CI job that builds the AAR on every pull request.
4. Library README with a ten line integration.

## Open for contribution

Each item says what it touches and how to prove it works.

- **PLY and .splat input** (help wanted).
  A `decodePly` and `decodeSplat` in `splat-core/formats` returning the same `SplatCloud`, with the RDF to RUB conversion the SPZ decoder does, wired into `detectSplatFormat`.
  Proof: unit tests with hand built files, and the same scene loaded from PLY and SPZ sorting and rendering the same.
- **SOG input** (help wanted).
  SOG (Spatially Ordered Gaussians, from PlayCanvas SuperSplat) stores a scene as a `meta.json` plus WebP images, about a tenth of the size of a PLY, and it is what the fastest Android viewers load.
  A `decodeSog` in `splat-core/formats` returning the same `SplatCloud`, plus a branch in `detectSplatFormat` (see [ADR 0008](adr/0008-one-decode-entry-point.md)), needs a WebP decoder in core (libwebp, pinned like spz) and the lookup table decode for scales, colours and rotations.
  Proof: a scene exported from SuperSplat as SOG and as PLY renders the same, plus decode time on a device.
- **GPU sort** (help wanted, needs a real device).
  A compute radix or bitonic sort producing the order buffer on the GPU, gated behind a feature flag so the CPU sort stays the baseline.
  Proof: identical order to `DistanceSorter` in a test, and frame time on Adreno and Mali with 2M splats.
- **Fewer blended fragments** (help wanted, needs a real device).
  Fragments are the cost on Adreno 640: half the pixels halve the GPU time.
  Ideas with a measurable claim: tighter quad bounds from the projected ellipse, opacity aware culling of splats that cannot change a pixel, a depth aware early out.
  A compute prepass was measured 4 ms slower here; do not resubmit it without a number from a device.
  Also measured and rejected on the Mi 9 (house p50, base 44.9 ms): one triangle per splat instead of a quad 48.1 ms, a triangle only for splats under 1.5 px 48.9 ms, the draw split into 256k instance chunks 181 ms, and a compute gather into draw order 46.3 ms.
  With culling in place (house 34.2, kitchen 21.8), an octagon per splat gave house 38.9 and kitchen 19.4: it trades vertices for fragments and only pays where splats are large.
- **Mali validation** (help wanted, needs a Samsung or Pixel).
  Run the dev app, report validation messages, driver behaviour and frame times.
  A crash or a black screen with logs attached is a valuable report.
- **Swapchain pre-rotation**.
  The swapchain forces the identity transform, so the compositor rotates every frame on devices whose panel is not in the identity orientation.
  Proof: no `VK_SUBOPTIMAL_KHR` after a rotation and the same image.
- **Chunked world upload off the render thread**.
  `Engine::uploadPendingWorld` packs, uploads and waits inside `render`, which freezes the frame and needs about three times the world size at the peak.
- **Frustum cull margin derived from the projected extent**, so large splats near the edge do not pop.
- **Faster spatial reorder** (help wanted).
  `reorderSpatially` is a radix sort plus five array permutations: 470 ms for 2M splats on the Mi 9's loader thread against 66 ms on an M4 Pro, so the permutation, not the sort, is what the phone pays.
  Proof: the reorder time in the log, and the same order (the test suite checks it).
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
