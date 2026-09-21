# SplatKit

Native Gaussian splatting SDKs for iOS/Metal, Android/Vulkan and React Native, sharing a C++17 engine.
Experimental alpha: APIs and quality/performance tradeoffs are still evolving.

[Android API](packages/splatkit-android/README.md) · [iOS / SwiftPM](https://github.com/Xget7/splatkit-ios) · [React Native](packages/react-native-splatkit/README.md) · [Agent harness](docs/AGENT_HARNESS.md) · [Changelog](CHANGELOG.md)

![Walking a Gaussian splat capture in the React Native example](docs/media/example-walk.gif)

## Use

Android: API 29+, Vulkan 1.1, arm64-v8a.
The GPU path additionally checks subgroup and memory limits.
Maven Central has `io.github.xget7:splatkit-android:0.1.0-alpha09`, with host-driven walking, the render policy and `onWorldFrameReady`; see the [Android releases](https://github.com/Xget7/splatkit/releases).
To build current source:

```sh
cd apps/android-dev
./gradlew :splatkit:assembleRelease :splatkit:testDebugUnitTest
```

iOS: add [splatkit-ios](https://github.com/Xget7/splatkit-ios) to Swift Package Manager, version `0.1.0-alpha.4`.
Use the native view, forward lifecycle and load worlds asynchronously; see each SDK's README for examples.

## Implemented scope

| Capability | Metal | Vulkan |
|---|---|---|
| GPU visibility, compaction, stable radix, indirect drawing | Yes | Yes, capability-gated |
| Offline `.lodsplat` and GPU hierarchical selection | Yes | Yes |
| 16-bit quantized depth / two radix passes | Per-view policy approximation | Per-view policy approximation |
| SH degrees 0–3, walk/fly, touch, motion, loaded/drawn stats | Yes | Yes |
| Hybrid compute screen tiles | Experimental per-view opt-in, for dense close-up scenes | Not implemented |
| Per-view render policy and capabilities | Yes | Yes |
| React Native policy prop and events | iPhone 17 Pro validated | Mi 9 validated |

React Native: `npm install @splatkit/react-native@next`, published from [react-native-splatkit](https://github.com/Xget7/react-native-splatkit); the [example app](apps/react-native/README.md) starts from zero.
One policy prop drives both adapters.

`splat-core` owns formats, hierarchy and navigation; `splatkit-engine` owns orchestration; each native SDK owns its GPU resources and view lifecycle.
CPU loading/preprocessing and a bounded compatibility ordering path remain.
GPU rendering does not mean zero CPU work.

## Validation and limits

Vulkan stable-sort (through 3M), visibility, LOD-to-indirect and upload-pressure suites pass on an arm64 emulator and on a physical Mi 9 (Adreno 640).
On that Mi 9, kitchen 500k and house 2M render with Vulkan validation on, and 30-second turns are logged in the [benchmarks](docs/BENCHMARKS.md).
These are correctness checks and short runs, **not sustained benchmarks**; Mali is untested.

Vulkan limits include at most 3M visibility survivors and 2.2M LOD-selected nodes.
Overflow fails closed; source residency depends on driver buffer limits and available memory.
LOD parents, subpixel culling and depth quantization can change the image.
There is no universal 10M/30/60 FPS or lossless guarantee.

[Backend contracts/evidence](packages/splatkit-android/docs/VULKAN.md) · [Parity gates](docs/VALIDATION.md#remaining-parity-gates) · [Historical device measurements](docs/BENCHMARKS.md)

## Contribute

Use the [agent harness](docs/AGENT_HARNESS.md), [validation gates](docs/VALIDATION.md) and [build guide](CONTRIBUTING.md).
Include device/driver, world, settings and logs with performance reports.
Next acceptance work: Mali and more Adreno devices, lifecycle stress, reference-image comparisons, then Vulkan hybrid tiles.

[MIT license](LICENSE) · [Third-party licenses](THIRD_PARTY_LICENSES.txt).
