# Roadmap

What is built, what is measured, and what is deliberately deferred.
Items move out of "Deferred" only with a number from a real device behind them.

## Done

- `splat-core`: SPZ v2 to v4 decoding into the internal RUB frame, GLB collider decoding, radix distance sort on a background thread, uniform grid raycasts, character controller.
  Unit and integration tests, CI with ThreadSanitizer and AddressSanitizer.
- `splatkit-android`: Vulkan 1.1 renderer (swapchain, two frames in flight, validation layers in debug builds), instanced splat pipeline with back to front blending, walk and fly camera, touch and gyroscope input, `SplatSurfaceView`.
- `apps/android-dev`: loads a World Labs kitchen and its collider, logs decode, upload, sort and frame times.

Verified on the Android emulator only.
Emulator numbers say the logic works; they say nothing about Adreno or Mali.

## Next

1. Measure on a Xiaomi Mi 9 (Adreno 640): decode, upload, sort and frame time for 500k and 2M splats in a release build.
2. Scene domain: load state machine with explicit states and error codes, so the view can report progress and failures instead of logging them.
3. Chunked world upload off the render thread.
   Today `Engine::uploadPendingWorld` packs, uploads and waits inside `render`, which freezes the frame for the whole transfer and needs about three times the world size in memory at the peak.
4. React Native package (`packages/react-native-splat`) with a Nitro `HybridView` over `SplatSurfaceView`.
5. iOS engine on the shared core.

## Deferred, with the reason

- Compute prepass for the covariance projection.
  The vertex shader repeats the projection and eigen decomposition for the four corners of every splat, as MetalSplatter does.
  Measure the vertex cost on the Mi 9 first; a prepass adds a pipeline, a buffer and a barrier.
- GPU sort.
  The CPU radix sort takes about 17 ms for 500k splats on the emulator host and only runs when the camera translates.
  Revisit if the 2M sort exceeds a frame on the device.
- Real swapchain pre-rotation.
  The swapchain forces the identity transform, so the compositor rotates every frame on devices whose panel is not in the identity orientation.
  Costs bandwidth and power; needs the projection to absorb the surface transform.
- Frustum cull margin.
  Splats are culled when their centre is beyond 1.2 times the clip w.
  A large splat near the edge can pop; a bound derived from its projected extent fixes it.
- Half precision covariance and colour in the GPU layout, halving the 48 bytes per splat.
- Spherical harmonics of degree 1 to 3.
  World Labs exports degree 0 and the decoder already passes the coefficients through.
- Locality sort of the splat buffer at load time for cache friendlier draws.
- Pipeline cache persisted to disk to cut cold start time.
- zstd decoded size ceiling.
  The gzip trailer declares the decoded size and the decoder rejects payloads beyond `maxDecodedBytes` before inflating; zstd frames may omit it.
- Offscreen render test that draws three known splats and checks pixels, and a long stress run alternating loads, rotations and surface losses.
