# SplatKit

Native Gaussian splatting SDKs for Android/Vulkan and iOS/Metal, sharing a C++17 engine.
Experimental alpha: APIs and quality/performance tradeoffs are still evolving.

[Android API](packages/splatkit-android/README.md) · [iOS / SwiftPM](https://github.com/Xget7/splatkit-ios) · [Agent harness](docs/AGENT_HARNESS.md)

## Use

Android: API 29+, Vulkan 1.1, arm64-v8a. The GPU path additionally checks subgroup and memory limits.
See the [Android releases](https://github.com/Xget7/splatkit-android/releases) for artifacts.
Maven `0.1.0-alpha04` predates GPU ordering; `0.1.0-alpha05` is the new GPU integration.
To build current source:

```sh
cd apps/android-dev
./gradlew :splatkit:assembleRelease :splatkit:testDebugUnitTest
```

iOS: add [splatkit-ios](https://github.com/Xget7/splatkit-ios) to Swift Package Manager, version `0.1.0-alpha.2`.
Use the native view, forward lifecycle and load worlds asynchronously; see each SDK's README for examples.

## Implemented scope

| Capability | Metal | Vulkan |
|---|---|---|
| GPU visibility, compaction, stable radix, indirect drawing | Yes | Yes, capability-gated |
| Offline `.lodsplat` and GPU hierarchical selection | Yes | Yes |
| 16-bit quantized depth / two radix passes | Opt-in approximation | Internal opt-in approximation |
| SH degrees 0–3, walk/fly, touch, motion, loaded/drawn stats | Yes | Yes |
| Hybrid compute screen tiles | Experimental | Not implemented |
| React Native GPU controls | Pending | Pending |

`splat-core` owns formats, hierarchy and navigation; `splatkit-engine` owns orchestration;
each native SDK owns its GPU resources and view lifecycle.
CPU loading/preprocessing and a bounded compatibility ordering path remain.
GPU rendering does not mean zero CPU work.

## Validation and limits

Vulkan passed 128 stable-sort cases through 3M, visibility, LOD-to-indirect integration,
and upload-pressure checks on an arm64 Android emulator using the Mac GPU.
Kitchen 500k rendered with Vulkan validation enabled and no captured errors.
These are functional checks, **not physical Android benchmarks**.

Vulkan limits include at most 3M visibility survivors and 2.2M LOD-selected nodes.
Overflow fails closed; source residency depends on driver buffer limits and available memory.
LOD parents, subpixel culling and depth quantization can change the image.
There is no universal 10M/30/60 FPS or lossless guarantee.

[Backend contracts/evidence](packages/splatkit-android/docs/VULKAN.md) ·
[Parity decision](docs/adr/0021-mobile-backend-parity.md) ·
[Historical device measurements](docs/BENCHMARKS.md)

## Contribute

Use the [agent harness](docs/AGENT_HARNESS.md) and [build guide](CONTRIBUTING.md).
Include device/driver, world, settings and logs with performance reports.
Next acceptance work: physical Adreno/Mali, lifecycle stress, reference-image comparisons,
then Vulkan hybrid tiles and React Native integration.

[MIT license](LICENSE) · [Third-party licenses](THIRD_PARTY_LICENSES.txt).
