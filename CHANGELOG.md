# Changelog

Notable changes to the SplatKit Android SDK, the shared C++ engine and this repository's tooling.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); alphas may break APIs.
iOS changes are in the [iOS changelog](packages/splatkit-ios/distribution/CHANGELOG.md) and React Native changes in the [React Native changelog](packages/react-native-splatkit/CHANGELOG.md).

## Unreleased

### Fixed

- The React Native mirror's `publish.yml` no longer passes `--prerelease`, so `react-native-splatkit` shows a Latest release the way the Android and iOS repositories already do.
- The React Native example builds without warnings on either platform.
  Its `Info.plist` no longer declares an empty `NSLocationWhenInUseUsageDescription`, which the app never used and App Store validation rejects, and `MainActivity` uses the `DefaultReactActivityDelegate` constructor React Native 0.87 has not deprecated.

## [0.1.0-alpha09] - 2026-09-21

### Added

- The notices bundled into the Android artifact name splat-transform (PlayCanvas), whose collision voxel passes `splat-core`'s collider builder ports, and carry its MIT licence.

### Changed

- A version bump is the release trigger.
  Landing a change to the Maven coordinate in `packages/splatkit-android/build.gradle.kts` on `main` publishes to Maven Central, tags the commit and opens the GitHub release; nobody cuts a tag by hand.
  `docs/RELEASING.md` describes all three artifacts and the order they have to be cut in.
- Android releases are no longer marked prerelease.
  Every pre-1.0 release is an alpha, and marking them all prerelease left the releases page with no Latest at all.

### Fixed

- The copyright holder in `LICENSE` and in the notices bundled into the Android artifact reads Juan Ignacio Andrade.
- `mirror.yml` names a missing `MIRROR_TOKEN` instead of failing four steps later with an opaque git authentication error, and waits for the test workflows on the same commit before it mirrors.
- The release gate fails fast when it cannot read workflow runs, instead of treating a run it could not read as a pass.
- `scripts/package-ios.sh` is executable, so the invocation `docs/RELEASING.md` documents runs.

### Removed

- `SplatSurfaceView.motionToggleEnabled` and the double tap that toggled the gyroscope.
  A host that wants the gesture recognises it on its own view and calls `setMotionEnabled`, which keeps the touches the SDK claims down to the single drag it documents.

## [0.1.0-alpha08] - 2026-09-18

### Added

- `RenderPolicySupport::rasterMask` in the shared engine, listing the raster strategies a backend builds.
- `RenderPolicy::lodSplatLimit` in the shared engine: a live cap on selected hierarchy splats, 0 for the loaded capacity.
- `buildCollider` in splat-core makes a walk-mode collider from a world's splats, a port of PlayCanvas splat-transform's collision voxel passes, with `encodeGlb` and `tools/splat_collider` to write it as a `.glb`.
- Shared engine stats count frames the display showed when a renderer reports presentation times, with a 95th percentile frame time, a 1% low and dropped frames; Metal reports them, Vulkan still counts submitted frames.
- `SplatSurfaceView.walk`, `setCharacter`, `character` and the `CharacterSettings` type, so the host drives walking and shapes the walker.
- `SplatSurfaceView.cameraPoseIntervalMillis` and `cameraPoseListener`, which report where the camera ended up at most that often and only while it moves.
- `SplatSurfaceView.touchLookEnabled` and `motionToggleEnabled`, for a host that draws its own look control.

### Changed

- Walk mode refuses steps onto a floor more than 0.35 m higher, looking 0.25 m ahead, so it climbs stairs and steps over door tracks but no longer climbs counters, chairs or tables whose top the hip probe passes over, and slides along them when walked into at an angle.
- The view handles one drag to look and nothing else; walking comes from the host.
  A second finger no longer walks, so the host's own controls keep every touch the look drag does not.
- The [React Native example](apps/react-native/README.md) replaces the React Native dev app: a template React Native 0.87.1 app that installs `@splatkit/react-native` from npm and runs on Android and iOS.

### Removed

- `SplatSurfaceView.walkSensitivity`, which configured the two-finger walk gesture that is gone.

## [0.1.0-alpha07] - 2026-09-16

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
- The React Native dev app, which mounts the Fabric package on Android.

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

[0.1.0-alpha09]: https://github.com/Xget7/splatkit/releases/tag/v0.1.0-alpha09
[0.1.0-alpha08]: https://github.com/Xget7/splatkit/releases/tag/v0.1.0-alpha08
[0.1.0-alpha07]: https://github.com/Xget7/splatkit/releases/tag/v0.1.0-alpha07
[0.1.0-alpha06]: https://github.com/Xget7/splatkit/releases/tag/v0.1.0-alpha06
[0.1.0-alpha05]: https://github.com/Xget7/splatkit/releases/tag/v0.1.0-alpha05
[0.1.0-alpha04]: https://github.com/Xget7/splatkit/releases/tag/v0.1.0-alpha04
[0.1.0-alpha03]: https://github.com/Xget7/splatkit/tree/v0.1.0-alpha03
[0.1.0-alpha02]: https://github.com/Xget7/splatkit/tree/v0.1.0-alpha02
