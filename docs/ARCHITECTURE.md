# Architecture

How the packages fit together and what each stage of the splat pipeline does on Metal and Vulkan.
Terms follow [CONTEXT.md](../CONTEXT.md); the linked files are the source of truth.
Before changing the pipeline for performance, follow the [prior-results review](gpu-refs/README.md#before-implementation).

## Packages

`splat-core` -> `splatkit-engine` -> `splatkit-ios` | `splatkit-android` -> `react-native-splatkit` -> `apps/react-native`

[Code layout](../CONTRIBUTING.md#code-layout) says what each package and folder owns.
Around the packages:

- [scripts](../scripts): world preparation (`prepare-world.sh`), iOS packaging and source export, the [SDK harness](AGENT_HARNESS.md), Perfetto and benchmark analysis, the shared [GPU experiment runner](GPU_PROFILING.md), and their tests.
- [.github/workflows](../.github/workflows): one workflow per package plus lint, release and the iOS mirror; [ci-filters.yml](../.github/ci-filters.yml) maps changed files to jobs.
- [VALIDATION.md](VALIDATION.md), [BENCHMARKS.md](BENCHMARKS.md), [RELEASING.md](RELEASING.md) and [VULKAN.md](../packages/splatkit-android/docs/VULKAN.md) for checks, measurements, releases and the Vulkan contracts.

## Load (shared, CPU)

Both platforms run the same code up to the upload.

SPZ tree construction runs locally on the loader CPU when the splat budget is positive.
The tree stays in memory; loading an SPZ does not write a `.lodsplat` file.
The optional [`splat_lod_build`](../packages/splat-core/tools/splat_lod_build.cpp) tool saves a prepared tree as `.lodsplat`; loading that file reads the tree instead of rebuilding it.

1. **Decode** on a loader thread.
   SPZ v1 to v4 goes through Niantic's spz, fetched and pinned by [dependencies.cmake][deps] and built with its extensions for the frame tag; [SpzDecoder.cpp][spz] inflates gzip with zlib, and spz handles zstd for v4.
   Every decoder moves its input from the file's coordinate frame into the engine's `rub` with [FrameTransform.cpp][frame-transform]: the host's source frame, else the frame the file declares, else its format's default.
   A `.lodsplat` carries its tree and skips steps 3 and 4.
   A tileset reads only `tileset.json` at load; two `TileLoader` threads decode its tiles later.
   PLY is read only by the offline tools.
   Loader threads: iOS serial queue `com.splatkit.loader` ([RenderThread.swift][ios-thread]), React Native iOS `com.splatkit.rn.loader`, Android single-thread executor `SplatKitLoader` ([RenderThread.kt][android-thread]).
2. **Bake** per splat inside the decoder, scalar: covariance `R diag(s)^2 R^T` ([Covariance.cpp][cov]), sigmoid opacity, the band 0 spherical harmonics (SH) folded into colour, SH truncated to `maxShDegree`.
3. **Reorder** by 30-bit Morton code ([SpatialOrder.cpp][morton]), plain SPZ only.
4. **Tree**: with a splat budget above 0, `buildLodTree` builds an adaptive octree at load with the same builder used offline ([LodTree.cpp][tree]); a budget of 0 draws the flat cloud.
5. **Pack** with [GpuLayout.cpp][layout]: `GpuSplat` is 32 bytes (position `f32x3`, `rgba8`, six half covariance terms, `lodAlpha`); SH sits in its own buffer, bands 1 to the degree, as half pairs by default; Vulkan can opt into [scaled 8-bit storage](#vulkan-sh-storage-experiment).

The decoded cloud waits in [SplatEngine.cpp][engine] until the render thread takes it and uploads it.

## Frame (per backend)

| Stage | Metal (iOS) | Vulkan (Android) |
|---|---|---|
| Upload | Zero copy: the packers write straight into `Shared` buffers on unified memory; tiles pack into a shared slab ([MetalWorld.mm][mtl-world]) | A world, its LOD clusters and leaves, and streamed tiles pack straight into device-local memory the CPU maps, coherent where offered, as on Metal. A device without such memory stages a world in 2 MiB host-visible windows, each a one-shot copy with a fence wait and `ALL_COMMANDS`/`TRANSFER` barriers ([VulkanBuffer.cpp][vk-buffer], [VulkanWorld.cpp][vk-world]) |
| Selection | GPU frontier traversal of the tree by pixel error within the splat budget, with cluster frustum culling, seven dispatches per tree level in one serial compute encoder of its own command buffer; the CPU adapts the pixel threshold from counters read back one to two frames late ([MetalLodSelection.mm][mtl-lod], [SplatLOD.metal][mtl-lod-shader]) | GPU frontier traversal of the tree by pixel error within the splat budget (binary search over prefix costs), with cluster frustum culling, seven dispatches per tree level; the CPU adapts the pixel threshold from counters read back one to two frames late ([lod_selection.comp][vk-lod], [LodThreshold.h][lod-threshold]) |
| Cull | `visibility` kernel culls alpha, behind camera, near/far, screen edge and subpixel; SIMD prefix compaction; writes keys and a 32-byte `ProjectedSplat` with SH evaluated ([SplatVisibility.metal][mtl-vis], [SplatProjection.metalh][mtl-proj]) | CPU validates the camera once and uploads near/far in the existing 192-byte uniform; invalid cameras submit a clear frame without compute or a world draw ([VulkanCameraUniforms.cpp][vk-camera]). `visibility.comp` culls alpha, behind camera, near/far by centre and by covariance, screen edge and subpixel; subgroup compaction; writes keys, values and a 32-byte `ProjectedSplat` with SH evaluated, one pipeline per SH degree ([visibility.comp][vk-vis], [splat_projection.glsl][vk-proj]) |
| Sort | GPU LSD radix argsort, 8-bit digits, 2 passes on 16-bit keys (default) or 4 on 32-bit; the first pass takes each key's index as its value; histogram and scatter rank with `simd_ballot`; indirect dispatch; needs 32-wide SIMD groups ([SplatRadixSort.metal][mtl-sort]) | GPU stable LSD radix, 8-bit digits, 4 or 2 passes; histogram with workgroup atomics because ballot loses keys on Adreno 640; scatter ranks each 128-key row with an `atomicOr` bitmask, the block's keys and values held in registers; indirect dispatch; 4M capacity ([radix_histogram.comp][vk-hist], [VulkanRadixSort.cpp][vk-sort]) |
| Raster | Instanced quads in 4 indirect draws, front to back with "under" blending, in batches of 8, 8, 16 and 32 sixty-fourths; between batches a mask pass writes `Depth16` where alpha reaches 254/255, so later batches skip saturated pixels; only the mask writes depth ([MetalSplatPipeline.mm][mtl-pipe], [SplatRaster.metal][mtl-raster], [PrepareIndirect.metalh][mtl-indirect]) | One indirect draw of instanced quads, back to front with "over" blending, one subpass with one colour attachment; `projected.vert` expands each quad from its record ([VulkanSplatPipeline.cpp][vk-pipe], [projected.vert][vk-projected]) |
| Present | With GPU sort, always an RGBA16F target, then a fullscreen render-pass blit (or MetalFX spatial upscale) into a BGRA8 drawable ([MetalSurface.mm][mtl-surface], [MetalUpscaler.mm][mtl-upscale]) | Straight into the swapchain at render scale 1, otherwise offscreen plus `vkCmdBlitImage`; a UNORM swapchain unless linear blending is on, since an sRGB attachment costs about 40% of the frame on Adreno 640; pre-rotated: the images keep the display's native orientation, the swapchain takes the display's rotation as its pre-transform and the vertex stage turns clip space, so the compositor scans the images out without a GPU pass ([VulkanSwapchain.cpp][vk-swap], [VulkanSurface.cpp][vk-surface]) |
| Submission | Three command buffers per frame (selection; cull, sort and indirect arguments; render and present), 2 frames in flight ([MetalSplatRenderer.mm][mtl-renderer]) | One command buffer per frame (selection, cull, sort, indirect arguments, diagnostics copy, render pass), 2 frames in flight, a swapchain of at least 3 images ([VulkanSplatRenderer.cpp][vk-renderer]) |

Both backends:

- Splats blend in the encoded space by default; linear blending (off by default) applies `pow(rgb, 2.2)` to splat colour and presents to an sRGB surface.
- The render policy applies per backend: both honour the sort depth and the LOD pixel error and splat limit, live ([VulkanFrameCompute.h][vk-frame]).
- The sub-pixel cutoff rides in the camera uniform, so it applies from the next frame on both order paths: a splat whose source sigma times its quad radius is under it has no quad ([SplatProjection.metalh][mtl-proj], [splat_projection.glsl][vk-proj]).
- Splats rasterize as hardware quads over the whole screen.
- Vulkan creates every pipeline through one `VkPipelineCache` kept in the app's cache directory, so a launch after the first reuses the driver's compiles; a file that fails its checksum or names another device or driver loads as an empty cache ([VulkanPipelineCache.cpp][vk-cache]). Metal keeps no file: the system caches compiled shaders on the device.
- A splat's quad ends where its Gaussian falls to 2/255, at most 2.5 sigma (5 for LOD nodes) and 512 px; a splat with a lower peak opacity is culled ([SplatTypes.metalh][mtl-types], [splat_footprint.glsl][vk-footprint]).
- The fragment stage shades in half precision: the vertex stage hands it log2 of the opacity and an offset scaled for `exp2`, so it discards under 1/255 before evaluating the falloff ([SplatRaster.metal][mtl-raster], [splat.frag][vk-frag]).
- Each renderer compiles its visibility, sort and selection pipelines once and keeps them across worlds ([MetalFrameCompute.mm][mtl-frame], [VulkanFrameCompute.h][vk-frame]); a world load only allocates buffers and descriptors. On Metal a change of sort key width rebuilds the visibility pipelines; Vulkan pushes the width as a constant.
- The radix sort's last pass writes only the values; the sorted keys are never read.
- [SplatEngine.cpp][engine] draws only when something marks a redraw, so a still view costs no GPU time.
- A streamed tile is written into a slab range only once every frame that drew the range finished: [TileStreamer.h][streamer] pins a replaced set until the renderer's completed frame serial passes the last frame that drew it.
- Collision runs on the CPU in the walk camera and moves only the camera.
- Both share one 32-byte `ProjectedSplat` ([GpuLayout.h][layout-h]); the visibility pass projects each splat once and sorts positions in that buffer.
- Without GPU sort, Metal falls back to a CPU order with one back-to-front "over" draw whose vertex stage projects each splat; Vulkan keeps the same path in [splat.vert][vk-vert] but needs subgroup basic, ballot and arithmetic support and refuses to load a world without it.

### Vulkan SH storage experiment

`SPLATKIT_VULKAN_SH8`, off by default, packs four 8-bit SH coefficients per word with a per-splat float scale ([GpuLayout.cpp][layout]); Metal retains half coefficients.
Degree-1 records shrink from 20 to 16 bytes, degree-3 from 92 to 52; the shared load-memory guard retains its conservative half-SH estimate.
[Vulkan](../packages/splatkit-android/docs/VULKAN.md#sh-storage-experiment) documents the switch.

[deps]: ../packages/splat-core/cmake/dependencies.cmake
[spz]: ../packages/splat-core/src/formats/SpzDecoder.cpp
[frame-transform]: ../packages/splat-core/src/formats/FrameTransform.cpp
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
[mtl-frame]: ../packages/splatkit-ios/Sources/SplatKitCore/rendering/metal/frame/MetalFrameCompute.mm
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
[vk-camera]: ../packages/splatkit-android/src/main/cpp/rendering/vulkan/frame/VulkanCameraUniforms.cpp
[vk-frame]: ../packages/splatkit-android/src/main/cpp/rendering/vulkan/frame/VulkanFrameCompute.h
[vk-cache]: ../packages/splatkit-android/src/main/cpp/rendering/vulkan/device/VulkanPipelineCache.cpp

## Optional part labels

`SplatCloud::labels` is empty by default, otherwise it contains one unsigned 16-bit label per splat, with 0 unlabelled and 1 through 65535 identifying parts.
Spatial reorder preserves correspondence; a built tree preserves leaf labels and assigns each interior node the label with greatest total descendant leaf opacity, breaking ties toward the lower label.
Prepared trees carry labels in a separate SFGL sidecar in node order, including interior nodes.
The existing `.lodsplat` format is unchanged and does not serialize labels.

The additive `SplatWorldLoader::loadWorld(data, size, labels, labelsSize)` and `loadWorldFile(path, labelsPath)` overloads validate sidecar counts before publishing a world, leaving the pending world untouched on failure.
For SPZ the sidecar follows decoded source order before spatial reorder; for `.lodsplat` it follows stored node order.
Existing overloads remain unlabelled.
These overloads are also available on the shared C++ `SplatEngine`; native hosts can bind them without carrying renderer source changes.
Tiled label streaming is unsupported and labelled tile uploads are rejected.

### SFGL version 2

All integers are little endian, with no trailing bytes.

| Offset in bytes | Type | Value |
|---|---|---|
| 0 | ASCII, 4 bytes | `SFGL` |
| 4 | u16 | Version 2 |
| 6 | u16 | Label width 2 bytes |
| 8 | u32 | Number of nodes or source splats |
| 12 | u32 | Reserved, must be zero |
| 16 | u16[count] | One label per node or source splat |

`decodePartLabels` rejects version 1, an unsupported width, reserved bits, truncated or trailing data, and an expected count mismatch.
`encodePartLabels` writes the same layout.

### Styling and picking

The shared `Highlight` eases selected labels toward a cyan tint and increased brightness, and dims other labels over 0.25 seconds.
`SplatEngine::highlightParts(labels, count)` starts or retargets that fade on the render thread; an empty selection restores captured appearance and a successful new load resets highlights.
A direct renderer caller can supply `SplatRenderer::Frame::labelStyles`, a borrowed 65536-entry `LabelStyles` table copied during `draw`; null restores defaults.
`LabelStyle` contains tint RGB, tint amount, brightness and opacity plus padding, with a 32-byte stride on CPU, Metal and Vulkan.
Use finite nonnegative colour/brightness values, tint amount in [0, 1] and opacity in [0, 1]; opacity fades reduce the existing footprint.
The lookup covers label 0 as well as every part label.

GPU splats and projected records remain 32 bytes.
Unlabelled worlds allocate no label or style buffers.
Metal specializes visibility with function constant 7 set false, which removes the style work and buffer arguments 12 and 13; labelled visibility pipelines are built lazily and preserve the original pipeline objects.
Vulkan compiles a separate labelled visibility module with bindings 10 and 11, leaving the default module, ten-binding descriptor layout and existing pipeline families unchanged.
A labelled world adds two packed label bytes per node, rounded to whole uint32 words, and a 2 MiB style storage buffer per frame slot, copied only after that slot is available.
Both backends apply styles in visibility before rasterization and retain their existing blend order, sort and presentation stages.

`SplatEngine::setPartPickingEnabled(true)` before loading explicitly retains CPU leaf geometry for picking; the default retains none.
`pickPart(Ray)` returns an exact uint16 label, with 0 for a miss or an unlabelled occluder, on the render thread.
The ray direction must be finite and normalized.
`PickIndex::take` is also available directly and consumes positions, covariances, opacities and labels from its cloud.
Picking composites ray intersections with three-sigma Gaussian supports, nearest first, and selects the strongest label after at least half coverage.
This CPU approximation does not reproduce screen-space filtering, current adaptive node selection, style opacity or exact raster coverage; exact means label precision, not pixel-equivalent GPU picking.
It scans retained leaves and allocates hit scratch per pick, so latency and additional CPU memory require measurement on large worlds.

### Label validation and physical evidence

No phone measurements are claimed for labels.
Prior Metal measurements combine visibility and radix and do not isolate label cost; the Vulkan saturation-mask rejection remains applicable because labels do not change blending or presentation.
Use the [profiling protocol](GPU_PROFILING.md) and [capture protocol](GPU_CAPTURE.md), with the 12M reference world and physical acceptance requirements in [validation](VALIDATION.md).
Compare baseline `0ba2ee5` against this revision with no labels to check the default path, then compare labelled captured styles and an active highlight fade against the same unlabelled view.
Record world and sidecar hashes, full build commits, device/driver/OS, viewport, SH1, 1.8M budget, pose `[-7.25, 1.6, 5.25, 0, 0]`, identical 30-second orbit speed and starting thermal state.
Measure upload time, resident/peak CPU and GPU memory, FPS and frame/GPU milliseconds with repeated A-B-B-A runs; retain repeat ranges and absolute/percentage changes.
Measure pick latency separately with picking enabled, including retained leaf memory, wide labels, unlabelled occlusion and misses.
Capture matched images, reviewing label boundaries, transparency, LOD transitions and fades separately from the unlabelled image comparison.
If timing identifies an effect, capture timelines and matched frames outside timing runs, resolving visibility, selection, radix, raster and present only where actual GPU samples support them, plus bandwidth counters where available.
Power comparisons require unplugged matched-brightness iPhone runs with populated Power Profiler lanes; FPS is not a power measurement.
Sustained claims require identical repeated 30-second turns for 10-15 minutes with continuous thermal and memory evidence.

Copy `scripts/benchmarks/reference-12m.json` into ignored `build/`, fill device IDs and prepared build paths/commits, and run `nice -n 19 python3 scripts/gpu_benchmark.py plan --config build/labels.json --target metal`, followed by `run` with a new `--out` and `compare --manifest <out>/experiment.json`.
Use `--target vulkan` for the corresponding Android comparison, `--phase timeline` for diagnostic traces and `--phase power` for supported iPhone power capture.
The generated `run.json` preserves the world, pose, policy, warmup and orbit schedule.
The current dev-app run schema does not carry label sidecars or highlight requests, so labelled phone measurements additionally require host wiring to the C++ labelled load and highlight APIs before running this protocol; do not claim the plain run file exercises labels.
No simulator, Mac GPU or cross-compilation result establishes physical acceptance.
