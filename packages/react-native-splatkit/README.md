# SplatKit React Native

Fabric view for the SplatKit Metal and Vulkan renderers, with one renderer policy contract across JS and both native adapters.
Experimental alpha: APIs change before 1.0.
New Architecture only, on React Native 0.87, Android minSdk 29 arm64-v8a and iOS 17.
A fresh React Native 0.87.1 app ran the packed package on an iPhone 17 Pro; Android runs in the SplatKit RN dev app on a Mi 9.
One finger drags to look and two fingers walk.
Changes are listed in the [changelog](CHANGELOG.md).

## Install

```sh
npm install @splatkit/react-native
cd ios && pod install
```

Android autolinks through the app's `com.facebook.react` Gradle plugin, which also runs Codegen, and pulls `io.github.xget7:splatkit-android` from Maven Central.
iOS vendors `SplatKitCore.xcframework` from the matching `splatkit-ios` release, checksum-verified when the npm package is packed.
Set `SPLATKIT_IOS_XCFRAMEWORK_PATH` to a local framework build before `npm pack` to test unreleased SDK changes.
iOS 26 terminates apps that skip the UIScene lifecycle, so hosts built from the React Native 0.87 template need a scene delegate.
The [example app](https://github.com/Xget7/splatkit-android/tree/main/apps/react-native) lists every change from the template, starting from zero.

## Use

```ts
import {SplatKitBuilder, SplatKitView, toNativePolicyProp, toNativeViewProps} from '@splatkit/react-native';

const config = new SplatKitBuilder()
  .withWorld({
    requestId: 'lobby-1',
    filePath: '/absolute/path/lobby.spz',
    maxShDegree: 3,
  })
  .withPreset('high')
  .withPerformance({lodBudgetSplats: 2_500_000})
  .build(hostCapabilities);

hostLogger.warn(config.performance.diagnostics);
const props = toNativeViewProps(config);
const policy = toNativePolicyProp(config, revision);
// <SplatKitView {...props} policy={policy} onPolicyEvent={...} onCapabilities={...} />
```

`build()` preserves the complete requested policy and separately reports effective values and warnings.
Effective means resolved Fabric props, not an acknowledgment of native applied state; `onPolicyEvent` is that acknowledgment.
The bridge expresses `world`, `paused`, `renderScale`, `shDegree` and the versioned renderer `policy`.
LOD and residency capacities are carried inside `world`, so changing either requires a new world request.
Render scale, draw SH degree and both splat capacities have one authority in `withPerformance()`.
`withWorld()` accepts world identity, file path and load-time maximum SH degree, while `withRender()` accepts only `paused`.
This is an intentional pre-1.0 change from passing capacities to `withWorld()` or scale and SH to `withRender()`.

Presets are provisional SDK defaults and install complete values:

| Preset | Render scale | SH degree | LOD budget | Residency budget |
| --- | ---: | ---: | ---: | ---: |
| `highEnd` | 1.25 | 3 | 4,000,000 | 4,000,000 |
| `high` | 1.0 | 3 | 3,000,000 | 3,000,000 |
| `balanced` | 0.85 | 2 | 2,000,000 | 2,000,000 |
| `performance` | 0.65 | 1 | 1,000,000 | 1,000,000 |

Calling `withPreset()` replaces the current policy with that preset.
Calling `withPerformance()` applies manual overrides to the current preset and marks the request `manual`.
Budgets count splats, including each splat's loaded SH payload, and are not byte or memory guarantees.
Host-supplied limits can clamp valid budget requests and the world's maximum loaded SH degree can cap draw SH.
Every fallback is reported in `diagnostics`.
Malformed numbers, invalid enum values and unknown options fail instead of degrading.

Raster strategy, tile size, LOD error, thresholds, culling, early termination and sort depth travel in the `policy` prop.
Give each changed policy a new positive Int32 revision; 0 or less means no policy.
An effective policy field holds a value only when native capabilities say the adapter applies it.
Other fields stay `null` and warn `native-option-fallback`, or `native-support-unknown` before capabilities are known, when the request differs from the native default.
`targetFps` is optional, always resolves to `null` and does not enable dynamic quality.
`nativeCapabilitiesFromEvent()` turns `onCapabilities` into the snapshot `build()` accepts.
That event arrives once per engine, and each world load creates one, so build the first configuration with host limits and rebuild when it arrives.
Even when a snapshot identifies a possible fallback, the bridge does not claim the native renderer applied it.

## Native adapters

Native applies each new revision, and re-applies the current one to every new engine, then reports it in `onPolicyEvent`.
`applied` means the whole request landed, `warning` means a field fell back and `rejected` means the previous policy stayed.
Every event carries the effective policy; a rejection's `errorCode` is `INVALID_POLICY` or `POLICY_PREPARATION_FAILED`.

Both adapters decode off the render thread, tag world events with the load that produced them and drop stale events, and emit the shared failure codes `INVALID_REQUEST`, `WORLD_LOAD_FAILED` and `GPU_UNAVAILABLE`.
Both re-validate the policy prop: an unknown raster or sort depth is invalid, never a silent default.
The iOS adapter throttles stats snapshots to 2 Hz and releases its engine and display link on recycle, invalidate and background.
The Android adapter isolates each accepted replacement in its own engine.
Vulkan applies sort depth and the sub-pixel threshold with GPU visibility; Metal applies sort depth with GPU sort and the sub-pixel threshold only under tight culling.
