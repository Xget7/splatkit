# SplatKit React Native

`@splatkit/react-native` renders 3D Gaussian splat scenes natively on Metal (iOS) and Vulkan (Android) through one Fabric component, `SplatKitView`, and turns a collider mesh into a walkable, collidable scene.

[![npm version](https://img.shields.io/npm/v/@splatkit/react-native.svg)](https://www.npmjs.com/package/@splatkit/react-native)
[![license: MIT](https://img.shields.io/npm/l/@splatkit/react-native.svg)](LICENSE)
[![platform: iOS | Android](https://img.shields.io/badge/platform-iOS%20%7C%20Android-lightgrey.svg)](#requirements)

<!-- TODO(maintainer): add a short GIF or screenshot of SplatKitView here, e.g. ![SplatKit demo](docs/demo.gif) -->

> **Experimental alpha.** APIs change before 1.0.
> The package has been validated on an iPhone 17 Pro and a Mi 9 (Adreno 640); no performance numbers are published beyond that.

## Features

- Native renderer, not a WebGL or JS port: Metal on iOS, Vulkan on Android, behind one Fabric view.
- New Architecture (Fabric) only, driven by `@react-native/codegen`.
- Loads `.spz`, `.ply` and `.lodsplat` files, and tiled worlds that stream progressively, straight from an absolute local file path (no JS byte transport).
- LOD selection and residency streaming with host-tunable splat-count budgets.
- A single, versioned per-view render policy that both native adapters validate and report back through `onCapabilities` and `onPolicyEvent`.
- Walk-mode collision: load a collider GLB and the camera becomes a character that stands on floors, is stopped by walls and climbs steps.
- Host-driven navigation: the SDK draws no walking or look UI, and imperative commands drive the camera at touch rate with no React commit per frame.
- Throttled stats (`onStats`, at most 2 Hz) and camera pose (`onCameraPose`, on your own interval) events, so hosts can build a HUD or a minimap without flooding the bridge.

## Requirements

| | Minimum |
| --- | --- |
| React Native | `0.87.x` (peer dependency `~0.87.1`) |
| React | `^19.2.3` |
| Architecture | New Architecture (Fabric) enabled; `SplatKitView` has no legacy-bridge fallback |
| iOS | iOS 17.0+, Xcode with CocoaPods, a device or simulator slice matching `SplatKitCore.xcframework` |
| Android | API 29+ (`minSdkVersion 29`), Vulkan 1.1, `arm64-v8a` only |
| Node | `^22.13.0`, `^24.3.0`, or `>=26.0.0` |

## Installation

```sh
npm install @splatkit/react-native
cd ios && pod install
```

- **iOS**: the podspec vendors `SplatKitCore.xcframework`, fetched and checksum-verified when the npm package is packed.
  To test an unreleased native build, set `SPLATKIT_IOS_XCFRAMEWORK_PATH` to a local framework before `npm pack`.
  iOS 26 terminates apps that skip the UIScene lifecycle, so a React Native 0.87 template app needs a scene delegate; see the [example app](https://github.com/Xget7/splatkit-android/tree/main/apps/react-native).
- **Android**: the module autolinks through the app's `com.facebook.react` Gradle plugin, which also runs Fabric Codegen, and pulls `io.github.xget7:splatkit-android` from Maven Central.
  Set `minSdkVersion = 29` and `reactNativeArchitectures=arm64-v8a` in the host app.

## Quick start

```tsx
import {SplatKitBuilder, SplatKitView, toNativeViewProps} from '@splatkit/react-native';

// Conservative placeholder limits for the very first frame; each engine reports its
// real limits in onCapabilities, and the configuration should be rebuilt from those.
const INITIAL_CAPABILITIES = {
  limits: {
    maxLodCapacitySplats: 1_000_000,
    minResidencyCapacitySplats: 100_000,
    maxResidencyCapacitySplats: 1_000_000,
  },
  supportsComputeTiles: false,
  supportsHiZOcclusion: false,
  supportsSubgroups: false,
  maxTextureDimension: 4096,
};

function Scene({worldPath}: {worldPath: string}) {
  const configuration = new SplatKitBuilder()
    .withWorld({requestId: 'lobby', filePath: worldPath, maxShDegree: 3})
    .withPreset('balanced')
    .build(INITIAL_CAPABILITIES);

  return <SplatKitView style={{flex: 1}} {...toNativeViewProps(configuration)} />;
}
```

`filePath` must be an absolute, readable local path, never a URL: the view never fetches or receives splat bytes over the bridge.
One finger drag looks around by default (`touchLookEnabled`), and there is no walking control until you add one; see [Walking](#walking).

### Getting a world file onto a device

Worlds are not bundled with the app; push or copy them onto the device's sandbox and point `filePath` at that path.

Android, to the app's external files directory:

```sh
adb push world.spz /sdcard/Android/data/<your.application.id>/files/world.spz
```

iOS, to the app's Documents directory on a connected device:

```sh
xcrun devicectl device copy to --device <device-id> --domain-type appDataContainer \
  --domain-identifier <your.bundle.id> --source world.spz --destination Documents/world.spz
```

Restart the app after copying a new world file.

## Walking

The SDK deliberately ships no walking or look UI: `SplatKitView` handles only a one-finger drag to look and a double tap to toggle the gyroscope, and every other control is the host's own.

1. Load a collider mesh through the `collider` prop: `{requestId, filePath}`, an absolute path to a collider GLB.
2. Wait for `onColliderEvent` with `phase: 'ready'` before you show walking controls; `phase: 'failed'` carries `errorCode` and `message`.
3. Tune the walker's shape with the `character` prop: `{eyeHeight, bodyRadius, stepHeight}`, in meters, applied immediately and to any collider loaded later.
4. Drive the camera with `SplatKitCommands`, imported from the package root.
   Commands go straight to the native view, bypassing React's render and commit cycle, so a joystick or a look pad can drive the camera at touch rate.

```tsx
import {useRef} from 'react';
import {PanResponder, View} from 'react-native';
import {SplatKitCommands, SplatKitView} from '@splatkit/react-native';

const WALK_SPEED = 1.4; // meters per second at full stick deflection
const RADIUS = 62;

function Joystick({viewRef}: {viewRef: React.RefObject<React.ComponentRef<typeof SplatKitView>>}) {
  const responder = useRef(
    PanResponder.create({
      onStartShouldSetPanResponder: () => true,
      onPanResponderMove: (_event, gesture) => {
        const forward = Math.max(-1, Math.min(1, -gesture.dy / RADIUS)) * WALK_SPEED;
        const right = Math.max(-1, Math.min(1, gesture.dx / RADIUS)) * WALK_SPEED;
        const target = viewRef.current;
        if (target) SplatKitCommands.setWalkVelocity(target, forward, right);
      },
      onPanResponderRelease: () => {
        const target = viewRef.current;
        if (target) SplatKitCommands.setWalkVelocity(target, 0, 0);
      },
    }),
  ).current;

  return <View {...responder.panHandlers} style={{width: RADIUS * 2, height: RADIUS * 2, borderRadius: RADIUS}} />;
}

// const viewRef = useRef(null);
// <SplatKitView ref={viewRef} {...props} collider={{requestId: 'floor', filePath: colliderPath}}
//   character={{eyeHeight: 1.5, bodyRadius: 0.35, stepHeight: 0.35}} onColliderEvent={onColliderEvent} />
// {walking && <Joystick viewRef={viewRef} />}
```

This mirrors the thumb stick in the [example app](https://github.com/Xget7/splatkit-android/tree/main/apps/react-native): the SDK never renders it, the host owns it entirely, and it disappears whenever `onColliderEvent` has not reported `ready`.

## Performance and quality

`SplatKitBuilder` resolves a requested policy against host and native limits into a `SplatKitConfiguration` with `world`, `render` and `performance` (`requested`, `effective`, `diagnostics`):

```ts
import {SplatKitBuilder, nativeCapabilitiesFromEvent, toNativePolicyProp, toNativeViewProps} from '@splatkit/react-native';

const configuration = new SplatKitBuilder()
  .withWorld({requestId: 'lobby-1', filePath: '/absolute/path/lobby.spz', maxShDegree: 3})
  .withPreset('high')
  .withPerformance({lodBudgetSplats: 2_500_000})
  .build(capabilities); // a DeviceCapabilities snapshot, e.g. from nativeCapabilitiesFromEvent

const props = toNativeViewProps(configuration);
const policy = toNativePolicyProp(configuration, revision);
// <SplatKitView {...props} policy={policy} onCapabilities={onCapabilities} onPolicyEvent={onPolicyEvent} />
```

- `withWorld()` accepts world identity, file path and load-time maximum SH degree only.
- `withRender()` accepts only `paused`.
- `withPreset(preset)` replaces the current policy with one of the presets below.
- `withPerformance(options)` overrides fields on top of the current preset and marks the policy `manual`.
- `build(capabilities)` clamps requested LOD and residency budgets to host limits, caps `shDegree` at the world's `maxShDegree`, and records every fallback in `performance.diagnostics` instead of silently degrading; malformed numbers, invalid enums and unknown options throw.

### Presets

Every preset rasterizes in `hardware`, with frustum culling and early termination enabled.

| Preset | Render scale | SH degree | LOD budget | Residency budget | Tile size | LOD error px | Alpha threshold | Sub-pixel threshold | Hi-Z occlusion | Sort depth |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | :---: | ---: |
| `highEnd` | 1.25 | 3 | 4,000,000 | 4,000,000 | 16 | 0.75 | 1/255 | 0.35 | Yes | 32 |
| `high` | 1.0 | 3 | 3,000,000 | 3,000,000 | 16 | 1.0 | 1/255 | 0.5 | Yes | 32 |
| `balanced` | 0.85 | 2 | 2,000,000 | 2,000,000 | 16 | 1.25 | 1/255 | 0.65 | No | 16 |
| `performance` | 0.65 | 1 | 1,000,000 | 1,000,000 | 8 | 2.0 | 2/255 | 1.0 | No | 16 |

`withPerformance({raster: 'hybrid'})` opts into experimental screen-tile compositing, which only helps where many large translucent splats overlap one pixel (close-up interiors) and costs more on distant or sparse scenes.
Today only the iOS adapter builds `hybrid`; `computeTile` is not implemented by any adapter.
`onCapabilities.policyRasterMask` reports which raster strategies an adapter actually applies.

### The `policy` prop and revisions

`policy` carries the versioned renderer policy: `raster`, `tileSize`, `lodErrorPixels`, `alphaThreshold`, `subpixelThreshold`, `enableFrustumCulling`, `enableHiZOcclusion`, `enableEarlyTermination` and `sortDepth`, under a `revision`.
Native re-validates the whole policy and never trusts JS state; give every changed policy a new positive `revision`, since 0 or less means no policy.
Native re-applies the current revision to every new engine, which a world reload also creates, so expect `onPolicyEvent` again after a reload.

`onCapabilities` fires once per engine, before its first policy event, and reports splat-count limits, GPU feature support and exactly which policy fields that adapter applies.
Pass its payload through `nativeCapabilitiesFromEvent()` to get the `DeviceCapabilities` that `build()` expects, and rebuild your configuration when it arrives.

`onPolicyEvent` reports one of three phases: `applied` (the whole request landed), `warning` (one or more fields fell back; `message` explains why) or `rejected` (the previous policy stayed in effect, with `errorCode` `INVALID_POLICY` or `POLICY_PREPARATION_FAILED`).
Every `onPolicyEvent` carries the effective policy values, whichever phase it reports.

### Splat-count knobs

| Knob | Lives in | Effect |
| --- | --- | --- |
| `lodCapacitySplats` | `world` (via `withPerformance({lodBudgetSplats})`) | LOD tree budget; 0 disables load-time tree building. Changing it needs a new world load. |
| `residencyCapacitySplats` | `world` (via `withPerformance({residencyCapacitySplats})`) | Resident streaming splats, not a byte or memory guarantee. Changing it needs a new world load. |
| `maxShDegree` | `world` (`withWorld()`) | Load-time cap on stored spherical-harmonics degree; caps `shDegree` below. |
| `shDegree` | view prop (`withPerformance({shDegree})`) | Draw-time SH degree, 0-3, clamped to the world's `maxShDegree`. |
| `renderScale` | view prop (`withPerformance({renderScale})`) | Render resolution scale, `[0.1, 2]`. |

LOD and residency capacities travel inside the `world` request, so changing either requires a new `requestId` and reload; render scale, draw SH degree and the rest of the policy update in place through the `policy` and view props.

## API reference

### `SplatKitView` props

`SplatKitView` extends the standard React Native `ViewProps` (`style`, `pointerEvents`, ...).

| Prop | Type | Default | Description |
| --- | --- | --- | --- |
| `world` | `WorldRequest` | - | Load transaction: `requestId`, absolute `filePath`, `maxShDegree`, `lodCapacitySplats`, `residencyCapacitySplats`. Omit to render nothing. |
| `collider` | `ColliderRequest` | - | `{requestId, filePath}`, the walk-mode counterpart of `world`; an empty `requestId` releases walk mode. |
| `character` | `Character` | - | `{eyeHeight, bodyRadius, stepHeight}`, the walker's shape in meters. |
| `paused` | `boolean` | `false` | Freezes rendering. |
| `renderScale` | `number` | `1` | Render resolution scale. |
| `shDegree` | `number` (0-3) | `3` | Draw-time spherical-harmonics degree. |
| `linearBlending` | `boolean` | `false` | Blends in linear light instead of the encoded space training used. |
| `cullMarginDegrees` | `number` | `10` | CPU fallback's angular culling margin in degrees; the GPU path uses projected bounds instead. |
| `motionEnabled` | `boolean` | `false` | Drives the camera with the gyroscope; ignored where the sensor is missing. |
| `touchLookEnabled` | `boolean` | `true` | Whether a one-finger drag on the view turns the camera. |
| `lookSensitivity` | `number` | `0.004` | Radians per point dragged to look. |
| `cameraPoseInterval` | `number` | `0` | Seconds between `onCameraPose` events; `0` never sends one. |
| `policy` | `NativeRenderPolicy` | - | The requested renderer policy; see [The `policy` prop and revisions](#the-policy-prop-and-revisions). |

### Events

| Event | Payload | Notes |
| --- | --- | --- |
| `onWorldEvent` | `{requestId, phase: 'uploaded' \| 'frameReady' \| 'failed', loadedSplats, errorCode, message}` | Fires per phase of a world load; `errorCode`/`message` are set only on `failed`. |
| `onColliderEvent` | `{requestId, phase: 'ready' \| 'failed', errorCode, message}` | Fires when a collider finishes loading or fails. |
| `onStats` | `{requestId, loadedSplats, drawnSplats, frameMillis, frameTimingAvailable, gpuMillis, gpuTimingAvailable, sortMillis, sortTimingAvailable}` | Throttled to at most 2 Hz by the adapter; a `*TimingAvailable` flag of `false` means its paired value is not meaningful. |
| `onCameraPose` | `{x, y, z, yaw, pitch}` | Throttled to `cameraPoseInterval`, in the world's frame, and sent only when the pose changed. |
| `onPolicyEvent` | `{revision, phase: 'applied' \| 'warning' \| 'rejected', errorCode, message, raster, tileSize, lodErrorPixels, alphaThreshold, subpixelThreshold, enableFrustumCulling, enableHiZOcclusion, enableEarlyTermination, sortDepth}` | One per policy application, including re-application to each new engine a world load creates. |
| `onCapabilities` | `{maxLodCapacitySplats, minResidencyCapacitySplats, maxResidencyCapacitySplats, supportsComputeTiles, supportsHiZOcclusion, supportsSubgroups, maxTextureDimension, policyRaster, policyRasterMask, policyTileSize, policyLodErrorPixels, policyAlphaThreshold, policySubpixelThreshold, policyEnableFrustumCulling, policyEnableHiZOcclusion, policyEnableEarlyTermination, policySortDepth}` | Emitted once per engine, before its first `onPolicyEvent`. |

### Commands (`SplatKitCommands`)

Exported from the package root; each command takes the `SplatKitView` ref as its first argument and returns nothing.

| Command | Signature | Notes |
| --- | --- | --- |
| `setWalkVelocity` | `(ref, forward: number, right: number)` | Meters per second, held until called again; forward is where the camera looks. Requires walk mode to be `ready`. |
| `look` | `(ref, deltaYaw: number, deltaPitch: number)` | Radians. Pitch is clamped; ignored while the gyroscope drives the view. |
| `setCameraPose` | `(ref, x: number, y: number, z: number, yaw: number, pitch: number)` | Teleports the camera; while walking, it settles onto the floor under the new point. |

### Contract types and validators (`src/contracts.ts`)

| Export | Shape / signature |
| --- | --- |
| `SHDegree` | `0 \| 1 \| 2 \| 3` |
| `WorldRequest` | `{requestId, filePath, maxShDegree, lodCapacitySplats, residencyCapacitySplats}` |
| `RenderOptions` | `{paused, renderScale, shDegree}` |
| `ColliderRequest` | `{requestId, filePath}` |
| `Character` | `{eyeHeight, bodyRadius, stepHeight}` |
| `ColliderEvent` | `{requestId, phase, errorCode, message}` |
| `CameraPose` | `{x, y, z, yaw, pitch}` |
| `SplatLimits` | `{maxLodCapacitySplats, minResidencyCapacitySplats, maxResidencyCapacitySplats}` |
| `WorldEvent` | `{requestId, phase, loadedSplats, errorCode, message}` |
| `validateWorldRequest(request, limits)` | Throws on structural violations; does not touch the filesystem or the GPU. |
| `validateColliderRequest(request)` | Throws unless `requestId` is nonempty and `filePath` is absolute. |
| `validateCharacter(character)` | Throws unless the walker's shape is finite and internally consistent. |
| `validateRenderOptions(options)` | Throws unless `paused`, `renderScale` and `shDegree` are in range. |
| `optionalTimingMillis(available, value)` | Returns `value`, or `null` when `available` is `false` (zero is a valid measurement). |

### Performance types and helpers (`src/performance.ts`)

| Export | Kind | Purpose |
| --- | --- | --- |
| `SplatKitBuilder` | class | `withWorld()`, `withRender({paused})`, `withPerformance(options)`, `withPreset(preset)`, `build(capabilities)` -> `SplatKitConfiguration`. |
| `toNativeViewProps(configuration)` | function | Returns the `world`, `paused`, `renderScale`, `shDegree` view props. |
| `toNativePolicyProp(configuration, revision)` | function | Returns the `policy` prop for a positive `revision`. |
| `nativeCapabilitiesFromEvent(event)` | function | Converts an `onCapabilities` payload into a `DeviceCapabilities` snapshot for `build()`. |
| `classifyPolicyChange(previous, next)` | function | Classifies a `PerformancePolicy` change as `'none' \| 'nativePropUpdate' \| 'worldReload' \| 'unavailable'`. |

Supporting types: `RasterStrategy` (`'hardware' \| 'computeTile' \| 'hybrid'`), `SortDepth` (`16 \| 32`), `PerformanceMode` (`'auto' \| 'manual'`), `QualityPreset` (`'highEnd' \| 'high' \| 'balanced' \| 'performance'`), `NativePolicySupport`, `DeviceCapabilities`, `NativeRenderPolicy`, `NativeCapabilitiesEvent`, `SplatKitWorldRequest`, `PerformancePolicy`, `PerformanceOptions`, `EffectivePerformancePolicy`, `PerformanceResolution`, `SplatKitConfiguration`, `PolicyChangeKind`.

`PerformanceDiagnostic` (`{severity: 'warning', code, option, requested, fallback, message}`) explains every fallback `build()` makes:

| `code` | Meaning |
| --- | --- |
| `limit-clamped` | The value was clamped to a host- or native-supplied limit. |
| `native-option-fallback` | Native capabilities say this field is not applied; it falls back to the native default. |
| `native-support-unknown` | Native capabilities have not arrived yet, so support for this field is unknown. |
| `capability-fallback-unavailable` | The requested raster strategy is unsupported; `hardware` is the candidate fallback. |
| `fabric-option-unavailable` | The option has no corresponding Fabric prop (`targetFps` only, which is a request only and never enables dynamic quality). |

## Error codes

Both native adapters share the same failure codes, so a host can branch on `errorCode` without checking platform:

| Code | Reported on | Meaning |
| --- | --- | --- |
| `INVALID_REQUEST` | `onWorldEvent`, `onColliderEvent` | The `world` or `collider` request failed structural validation before native ever tried to load it. |
| `WORLD_LOAD_FAILED` | `onWorldEvent` | The world file could not be decoded or loaded. |
| `GPU_UNAVAILABLE` | `onWorldEvent` | The GPU backend could not be initialized (for example, Vulkan device creation failed). |
| `COLLIDER_LOAD_FAILED` | `onColliderEvent` | The collider GLB could not be loaded. |
| `INVALID_POLICY` | `onPolicyEvent` | The `policy` prop failed validation (for example, an unknown raster strategy or sort depth). |
| `POLICY_PREPARATION_FAILED` | `onPolicyEvent` | The policy passed validation but native failed to prepare it; the previous policy stays in effect. |

## Troubleshooting

**The view is blank or black.**
Check that `world` is set and that `onWorldEvent` ever fires; a view with no `world` prop renders nothing.
On a simulator or emulator, GPU support is often missing or partial: prefer a physical device, especially on Android, where an emulator without full Vulkan 1.1 support reports `GPU_UNAVAILABLE`.

**The world fails to load (`onWorldEvent` with `phase: 'failed'`).**
`filePath` must be an absolute, local, readable path, not a URL, a `require()` asset or a content URI; the view does no fetching and no JS byte transport.
Confirm the file actually exists at that path in the app's sandbox (see [Getting a world file onto a device](#getting-a-world-file-onto-a-device)), and read `message` for the native loader's reason.

**Nothing renders and the app crashes or logs a Fabric/Codegen error.**
`SplatKitView` is Fabric-only; confirm the New Architecture is enabled and that `npx react-native codegen` (or a full rebuild) picked up `SplatKitSpec`.

**Simulator vs. device.**
The package has only been verified on physical devices, an iPhone 17 Pro and a Mi 9 (Adreno 640).
The iOS Simulator and Android emulators can differ in Metal/Vulkan feature support from real hardware; if `onCapabilities` reports unexpectedly low limits or `supportsComputeTiles`/`supportsHiZOcclusion` as `false`, try a device before filing an issue.

## Example app

[`apps/react-native`](https://github.com/Xget7/splatkit-android/tree/main/apps/react-native) is a React Native 0.87.1 app built from the community template that installs this package from npm, drags to look, double-taps to toggle the gyroscope, and drives a joystick through `SplatKitCommands` once a collider is ready.
Its README lists every change needed from a fresh template, including `minSdkVersion`, `arm64-v8a`, the iOS 17 deployment target and the scene delegate iOS 26 requires.

## Contributing

This package is exported from a monorepo; see [CONTRIBUTING.md](CONTRIBUTING.md) for how to build, test and send changes back.

## License

[MIT](LICENSE)
