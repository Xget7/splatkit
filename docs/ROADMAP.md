# Roadmap

SplatKit is experimental.
This page lists what works, what is next, and what is open for anyone to pick up.
Items marked "help wanted" have a defined scope and no owner; open an issue before starting so nobody duplicates the work.

## Works today

- `splat-core`: SPZ v2 to v4 decoding into the internal RUB frame, GLB collider decoding, radix distance sort on a background thread, uniform grid raycasts, character controller.
  Unit and integration tests, CI with ThreadSanitizer and AddressSanitizer.
- `splatkit-android`: Vulkan 1.1 renderer (swapchain, two frames in flight, validation layers in debug builds), instanced splat pipeline with back to front blending, walk and fly camera, touch, joystick and gyroscope input, `SplatSurfaceView`, `SplatHudView`.
- `apps/android-dev`: loads a World Labs kitchen and its collider, shows GPU, frame time, sort time and splat count.

Measured on a Xiaomi Mi 9 (Adreno 640, Vulkan 1.1.128), release build, 500k splats at 1080x2261, reproducible benchmark (`--ez benchmark true`):

| Render scale | GPU ms mean | GPU ms p50 |
|---|---|---|
| 1.0 | 27 to 32 | 25 to 29 |
| 0.7 | 18.7 | 16.7 |
| 0.5 | 15.0 | 13.7 |

Decode 192 ms, upload 73 ms, sort 11.5 ms on a background thread.
Vertex fetch is an 8 ms floor; vertex math costs nothing; blended fragments are the rest.
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
- **GPU sort** (help wanted, needs a real device).
  A compute radix or bitonic sort producing the order buffer on the GPU, gated behind a feature flag so the CPU sort stays the baseline.
  Proof: identical order to `DistanceSorter` in a test, and frame time on Adreno and Mali with 2M splats.
- **Fewer blended fragments** (help wanted, needs a real device).
  Fragments are the cost on Adreno 640: half the pixels halve the GPU time.
  Ideas with a measurable claim: tighter quad bounds from the projected ellipse, opacity aware culling of splats that cannot change a pixel, a depth aware early out.
  A compute prepass was measured 4 ms slower here; do not resubmit it without a number from a device.
- **Mali validation** (help wanted, needs a Samsung or Pixel).
  Run the dev app, report validation messages, driver behaviour and frame times.
  A crash or a black screen with logs attached is a valuable report.
- **Swapchain pre-rotation**.
  The swapchain forces the identity transform, so the compositor rotates every frame on devices whose panel is not in the identity orientation.
  Proof: no `VK_SUBOPTIMAL_KHR` after a rotation and the same image.
- **Chunked world upload off the render thread**.
  `Engine::uploadPendingWorld` packs, uploads and waits inside `render`, which freezes the frame and needs about three times the world size at the peak.
- **Frustum cull margin derived from the projected extent**, so large splats near the edge do not pop.
- **Half precision covariance and colour** in the GPU layout, halving 48 bytes per splat.
  The 8 ms vertex fetch floor is the target; measure with the benchmark.
- **Pipeline cache persisted to disk** to cut cold start time.
- **Offscreen render test** that draws three known splats and checks pixels, and a long stress run alternating loads, rotations and surface losses.

## Not planned

- iOS, macOS or visionOS inside this engine.
  The shared core is written so that a Metal engine can sit next to this one, but that is a separate package.
- React Native inside the engine.
  The React Native package wraps `SplatSurfaceView` and lives in `packages/react-native-splat`.
