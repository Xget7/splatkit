# Changelog

Notable changes to the SplatKit Android SDK, the shared C++ engine and this repository's tooling.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); alphas may break APIs.
iOS changes are in the [iOS changelog](packages/splatkit-ios/distribution/CHANGELOG.md) and React Native changes in the [React Native changelog](packages/react-native-splatkit/CHANGELOG.md).

## Unreleased

### Added

- `SplatSurfaceView.applyRenderPolicy`, `renderPolicy` and `deviceCapabilities`, with the `RenderPolicy`, `RenderPolicyResolution` and `DeviceCapabilities` types.
  The engine re-validates each request on the render thread; Vulkan applies `sortDepth` and `subpixelThreshold`, and every other field falls back with a warning.
  An invalid request or a preparation failure keeps the previous policy.
- `SplatSurfaceView.Listener.onWorldFrameReady`, fired once when the first world frame's Vulkan fence completes.
- `loadWorld(file, maxShDegree, splatBudget, residencyBudget)`, which applies load options on the render thread before that file's decode.
- 16 KB page alignment for the native library; CI checks every packaged shared library and APK ZIP alignment with `scripts/check_android_alignment.py`.
- Shared engine: `resolveRenderPolicy`, `SplatEngine::setRenderPolicy` and a capability query on `SplatRenderer`.
- Benchmarks log 30-second windows and final p99 frame and GPU times; `TimingSummary` reports p99.
- `scripts/benchmark_report.py` for sustained-run reports and `scripts/compare_captures.py` for matched-image comparisons.
- The [React Native dev app](apps/react-native-dev/README.md), which mounts the Fabric package on Android.

### Changed

- `readStats()` drawn counts and GPU times describe the newest frame the GPU finished, also while nothing redraws, and are current when `onWorldFrameReady` fires.
- Benchmarks reject durations outside `(0, 3600]` seconds and treat a zero GPU time as unavailable, not as a free frame.

### Removed

- The `SPLATKIT_VULKAN_SORT_BITS` environment variable; set `sortDepth` through `applyRenderPolicy` instead.

### Fixed

- The Vulkan frame-compute GPU test ran its 16-bit pass at 32 bits; it now selects the depth through the policy and asserts it.

## [0.1.0-alpha06] - 2026-09-12

### Fixed

- Adreno 640 duplicate-key corruption in the GPU radix sort of alpha05.

### Changed

- LOD visibility skips empty workgroups: house 2M on a Mi 9 went from 24.0 to 41.4 FPS with unchanged quality settings.

## [0.1.0-alpha05] - 2026-09-12

### Added

- Native GPU pipeline: LOD selection, visibility and compaction, stable radix sort and indirect drawing, behind runtime capability and memory gates.
- Offline `.lodsplat` worlds and live loaded and drawn counts.

### Known issues

- Adreno GPUs can corrupt the radix order; use alpha06.

## [0.1.0-alpha04] - 2026-09-09

### Added

- The harmonics degree is chosen per frame, so a preset switch never reloads the world.
- The cull margin scales with the frame time.
- Engine modules, `clang-format` and `clang-tidy` on every pull request, and the `ply2spz` converter.

### Fixed

- Truncated or corrupt files and failed swapchain rebuilds report through the listener instead of crashing.

## [0.1.0-alpha03] - 2026-09-07

### Added

- Quality presets `LOW` to `ULTRA`, render scale up to 2, the cull margin, `cameraPose` and file-mapped world and collider loading.
- Blending in the encoded colour space by default, and an opt-in level of detail tree.

## [0.1.0-alpha02] - 2026-09-04

### Added

- First Maven Central publication of the Vulkan SDK.

[0.1.0-alpha06]: https://github.com/Xget7/splatkit-android/releases/tag/v0.1.0-alpha06
[0.1.0-alpha05]: https://github.com/Xget7/splatkit-android/releases/tag/v0.1.0-alpha05
[0.1.0-alpha04]: https://github.com/Xget7/splatkit-android/releases/tag/v0.1.0-alpha04
[0.1.0-alpha03]: https://github.com/Xget7/splatkit-android/tree/v0.1.0-alpha03
[0.1.0-alpha02]: https://github.com/Xget7/splatkit-android/tree/v0.1.0-alpha02
