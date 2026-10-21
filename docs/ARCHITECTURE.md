# Architecture

How the packages fit together and what each stage of the splat pipeline does on Metal and Vulkan.
Terms follow [CONTEXT.md](../CONTEXT.md); the linked files are the source of truth.

## Packages

`splat-core` -> `splatkit-engine` -> `splatkit-ios` | `splatkit-android` -> `react-native-splatkit` -> `apps/react-native`

[Code layout](../CONTRIBUTING.md#code-layout) says what each package and folder owns.
Around the packages:

- [scripts](../scripts): world preparation (`prepare-world.sh`), iOS packaging and source export, the [SDK harness](AGENT_HARNESS.md), Perfetto and benchmark analysis, and their tests.
- [.github/workflows](../.github/workflows): one workflow per package plus lint, release and the iOS mirror; [ci-filters.yml](../.github/ci-filters.yml) maps changed files to jobs.
- [VALIDATION.md](VALIDATION.md), [BENCHMARKS.md](BENCHMARKS.md), [RELEASING.md](RELEASING.md) and [VULKAN.md](../packages/splatkit-android/docs/VULKAN.md) for checks, measurements, releases and the Vulkan contracts.

## Load (shared, CPU)

Both platforms run the same code up to the upload.

1. **Decode** on a loader thread.
   SPZ v1 to v4 goes through Niantic's spz, fetched and pinned by [dependencies.cmake][deps]; [SpzDecoder.cpp][spz] inflates gzip with zlib, and spz handles zstd for v4.
   A `.lodsplat` carries its tree and skips steps 3 and 4.
   A tileset reads only `tileset.json` at load; two `TileLoader` threads decode its tiles later.
   PLY is read only by the offline tools.
   Loader threads: iOS serial queue `com.splatkit.loader` ([RenderThread.swift][ios-thread]), React Native iOS `com.splatkit.rn.loader`, Android single-thread executor `SplatKitLoader` ([RenderThread.kt][android-thread]).
2. **Bake** per splat inside the decoder, scalar: covariance `R diag(s)^2 R^T` ([Covariance.cpp][cov]), sigmoid opacity, the band 0 spherical harmonics (SH) folded into colour, SH truncated to `maxShDegree`.
3. **Reorder** by 30-bit Morton code ([SpatialOrder.cpp][morton]), plain SPZ only.
4. **Tree**: with a splat budget above 0, `buildLodTree` builds a size-adaptive grid at load ([LodTree.cpp][tree]); a budget of 0 draws the flat cloud.
5. **Pack** with [GpuLayout.cpp][layout]: `GpuSplat` is 32 bytes (position `f32x3`, `rgba8`, six half covariance terms, `lodAlpha`); SH sits in its own buffer as half pairs, bands 1 to the degree.

The decoded cloud waits in [SplatEngine.cpp][engine] until the render thread takes it and uploads it.

## Frame (per backend)

| Stage | Metal (iOS) | Vulkan (Android) |
|---|---|---|
| Upload | Zero copy: the packers write straight into `Shared` buffers on unified memory; tiles pack into a shared slab ([MetalWorld.mm][mtl-world]) | A world: 2 MiB host-visible staging windows into device-local VMA buffers, each a one-shot copy with a fence wait and `ALL_COMMANDS`/`TRANSFER` barriers. Tiles pack straight into a slab mapped in device-local, host-visible memory, coherent where offered, and are staged only where the device has none ([VulkanBuffer.cpp][vk-buffer], [VulkanWorld.cpp][vk-world]) |
| Selection | GPU frontier traversal of the tree by pixel error within the splat budget, with cluster frustum culling, in its own command buffer; the CPU adapts the pixel threshold from counters read back one to two frames late ([MetalLodSelection.mm][mtl-lod], [SplatLOD.metal][mtl-lod-shader]) | GPU frontier traversal of the tree by pixel error within the splat budget (binary search over prefix costs), with cluster frustum culling; the CPU adapts the pixel threshold from counters read back one to two frames late ([lod_selection.comp][vk-lod], [LodThreshold.h][lod-threshold]) |
| Cull | `visibility` kernel culls alpha, behind camera, near/far, screen edge and subpixel; SIMD prefix compaction; writes keys, values and a 32-byte `ProjectedSplat` with SH evaluated ([SplatVisibility.metal][mtl-vis], [SplatProjection.metalh][mtl-proj]) | `visibility.comp` culls alpha, behind camera, near/far by centre and by covariance, screen edge and subpixel; subgroup compaction; writes keys, values and a 32-byte `ProjectedSplat` with SH evaluated, one pipeline per SH degree ([visibility.comp][vk-vis], [splat_projection.glsl][vk-proj]) |
| Sort | GPU LSD radix, 8-bit digits, 4 passes on 32-bit keys (default) or 2 on 16-bit; histogram and scatter rank with `simd_ballot`; indirect dispatch; needs 32-wide SIMD groups ([SplatRadixSort.metal][mtl-sort]) | GPU stable LSD radix, 8-bit digits, 4 or 2 passes; histogram with workgroup atomics because ballot loses keys on Adreno 640; scatter ranks each 128-key row with an `atomicOr` bitmask, the block's keys and values held in registers; indirect dispatch; 4M capacity ([radix_histogram.comp][vk-hist], [VulkanRadixSort.cpp][vk-sort]) |
| Raster | Instanced quads in 7 indirect draws, front to back with "under" blending, in batches of 1, 1, 2, 4, 8, 16 and 32 sixty-fourths; between batches a mask pass writes `Depth16` where alpha reaches 254/255, so later batches skip saturated pixels; only the mask writes depth ([MetalSplatPipeline.mm][mtl-pipe], [SplatRaster.metal][mtl-raster], [PrepareIndirect.metalh][mtl-indirect]) | One indirect draw of instanced quads, back to front with "over" blending, one subpass with one colour attachment; `projected.vert` expands each quad from its record ([VulkanSplatPipeline.cpp][vk-pipe], [projected.vert][vk-projected]) |
| Present | With GPU sort, always an RGBA16F target, then a fullscreen render-pass blit (or MetalFX spatial upscale) into a BGRA8 drawable ([MetalSurface.mm][mtl-surface], [MetalUpscaler.mm][mtl-upscale]) | Straight into the swapchain at render scale 1, otherwise offscreen plus `vkCmdBlitImage`; a UNORM swapchain unless linear blending is on, since an sRGB attachment costs about 40% of the frame on Adreno 640; pre-rotated: the images keep the display's native orientation, the swapchain takes the display's rotation as its pre-transform and the vertex stage turns clip space, so the compositor scans the images out without a GPU pass ([VulkanSwapchain.cpp][vk-swap], [VulkanSurface.cpp][vk-surface]) |
| Submission | Three command buffers per frame (selection; cull, sort and indirect arguments; render and present), 2 frames in flight ([MetalSplatRenderer.mm][mtl-renderer]) | One command buffer per frame (selection, cull, sort, indirect arguments, diagnostics copy, render pass), 2 frames in flight, a swapchain of at least 3 images ([VulkanSplatRenderer.cpp][vk-renderer]) |

Both backends:

- Splats blend in the encoded space by default; linear blending (off by default) applies `pow(rgb, 2.2)` to splat colour and presents to an sRGB surface.
- The render policy applies per backend: both honour the sort depth and the LOD pixel error and splat limit, live ([VulkanFrameCompute.h][vk-frame]).
- The sub-pixel cutoff rides in the camera uniform, so it applies from the next frame on both order paths: a splat whose source sigma times its quad radius is under it has no quad ([SplatProjection.metalh][mtl-proj], [splat_projection.glsl][vk-proj]).
- Splats rasterize as hardware quads over the whole screen; `supportsComputeTiles` is false on both.
- A splat's quad ends where its Gaussian falls to 2/255, at most 2.5 sigma (5 for LOD nodes) and 512 px; a splat with a lower peak opacity is culled ([SplatTypes.metalh][mtl-types], [splat_footprint.glsl][vk-footprint]).
- The fragment stage shades in half precision: the vertex stage hands it log2 of the opacity and an offset scaled for `exp2`, so it discards under 1/255 before evaluating the falloff ([SplatRaster.metal][mtl-raster], [splat.frag][vk-frag]).
- The radix sort's last pass writes only the values; the sorted keys are never read.
- [SplatEngine.cpp][engine] draws only when something marks a redraw, so a still view costs no GPU time.
- A streamed tile is written into a slab range only once every frame that drew the range finished: [TileStreamer.h][streamer] pins a replaced set until the renderer's completed frame serial passes the last frame that drew it.
- Collision runs on the CPU in the walk camera and moves only the camera.
- Both share one 32-byte `ProjectedSplat` ([GpuLayout.h][layout-h]); the visibility pass projects each splat once and sorts positions in that buffer.
- Without GPU sort, Metal falls back to a CPU order with one back-to-front "over" draw whose vertex stage projects each splat; Vulkan keeps the same path in [splat.vert][vk-vert] but needs subgroup basic, ballot and arithmetic support and refuses to load a world without it.

[deps]: ../packages/splat-core/cmake/dependencies.cmake
[spz]: ../packages/splat-core/src/formats/SpzDecoder.cpp
[cov]: ../packages/splat-core/src/math/Covariance.cpp
[morton]: ../packages/splat-core/src/sorting/SpatialOrder.cpp
[tree]: ../packages/splat-core/src/lod/LodTree.cpp
[lod-threshold]: ../packages/splatkit-engine/include/splatkit/rendering/LodThreshold.h
[layout]: ../packages/splatkit-engine/src/rendering/GpuLayout.cpp
[layout-h]: ../packages/splatkit-engine/include/splatkit/rendering/GpuLayout.h
[engine]: ../packages/splatkit-engine/src/engine/SplatEngine.cpp
[streamer]: ../packages/splat-core/include/splat/tiles/TileStreamer.h
[ios-thread]: ../packages/splatkit-ios/Sources/SplatKit/engine/RenderThread.swift
[android-thread]: ../packages/splatkit-android/src/main/kotlin/com/splatkit/engine/RenderThread.kt
[mtl-world]: ../packages/splatkit-ios/Sources/SplatKitCore/rendering/metal/world/MetalWorld.mm
[mtl-lod]: ../packages/splatkit-ios/Sources/SplatKitCore/rendering/metal/selection/MetalLodSelection.mm
[mtl-lod-shader]: ../packages/splatkit-ios/Sources/SplatKitCore/rendering/metal/selection/SplatLOD.metal
[mtl-vis]: ../packages/splatkit-ios/Sources/SplatKitCore/rendering/metal/cull/SplatVisibility.metal
[mtl-proj]: ../packages/splatkit-ios/Sources/SplatKitCore/rendering/metal/layout/SplatProjection.metalh
[mtl-types]: ../packages/splatkit-ios/Sources/SplatKitCore/rendering/metal/layout/SplatTypes.metalh
[mtl-sort]: ../packages/splatkit-ios/Sources/SplatKitCore/rendering/metal/sort/SplatRadixSort.metal
[mtl-pipe]: ../packages/splatkit-ios/Sources/SplatKitCore/rendering/metal/raster/MetalSplatPipeline.mm
[mtl-raster]: ../packages/splatkit-ios/Sources/SplatKitCore/rendering/metal/raster/SplatRaster.metal
[mtl-indirect]: ../packages/splatkit-ios/Sources/SplatKitCore/rendering/metal/cull/PrepareIndirect.metalh
[mtl-surface]: ../packages/splatkit-ios/Sources/SplatKitCore/rendering/metal/presentation/MetalSurface.mm
[mtl-upscale]: ../packages/splatkit-ios/Sources/SplatKitCore/rendering/metal/presentation/MetalUpscaler.mm
[mtl-renderer]: ../packages/splatkit-ios/Sources/SplatKitCore/rendering/metal/MetalSplatRenderer.mm
[vk-buffer]: ../packages/splatkit-android/src/main/cpp/rendering/vulkan/device/VulkanBuffer.cpp
[vk-world]: ../packages/splatkit-android/src/main/cpp/rendering/vulkan/world/VulkanWorld.cpp
[vk-lod]: ../packages/splatkit-android/src/main/cpp/rendering/vulkan/selection/lod_selection.comp
[vk-vis]: ../packages/splatkit-android/src/main/cpp/rendering/vulkan/cull/visibility.comp
[vk-footprint]: ../packages/splatkit-android/src/main/cpp/rendering/vulkan/layout/splat_footprint.glsl
[vk-proj]: ../packages/splatkit-android/src/main/cpp/rendering/vulkan/layout/splat_projection.glsl
[vk-hist]: ../packages/splatkit-android/src/main/cpp/rendering/vulkan/sort/radix_histogram.comp
[vk-sort]: ../packages/splatkit-android/src/main/cpp/rendering/vulkan/sort/VulkanRadixSort.cpp
[vk-pipe]: ../packages/splatkit-android/src/main/cpp/rendering/vulkan/raster/VulkanSplatPipeline.cpp
[vk-vert]: ../packages/splatkit-android/src/main/cpp/rendering/vulkan/raster/splat.vert
[vk-projected]: ../packages/splatkit-android/src/main/cpp/rendering/vulkan/raster/projected.vert
[vk-frag]: ../packages/splatkit-android/src/main/cpp/rendering/vulkan/raster/splat.frag
[vk-swap]: ../packages/splatkit-android/src/main/cpp/rendering/vulkan/presentation/VulkanSwapchain.cpp
[vk-surface]: ../packages/splatkit-android/src/main/cpp/rendering/vulkan/presentation/VulkanSurface.cpp
[vk-renderer]: ../packages/splatkit-android/src/main/cpp/rendering/vulkan/VulkanSplatRenderer.cpp
[vk-frame]: ../packages/splatkit-android/src/main/cpp/rendering/vulkan/frame/VulkanFrameCompute.h
