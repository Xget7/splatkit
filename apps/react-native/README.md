# SplatKit React Native example

A React Native 0.87.1 app, made with `npx @react-native-community/cli init`, that renders one full-screen `SplatKitView` you can walk through.
Drag anywhere to look around.
The world is one `.lodsplat`, the detail tree built offline by [Preparing a world](../../packages/react-native-splatkit/README.md#preparing-a-world); [`World.ts`](World.ts) names its file.
The example world is 12M splats, too many to load as one `.spz` on an iPhone 17 Pro; with its tree built offline it loads in seconds.
Once the collider is ready a thumb stick appears; it is this app's own control, in [`Joystick.tsx`](Joystick.tsx), and it drives the view through `SplatKitCommands.setWalkVelocity`.
After loading, `Orbit` applies a revisioned camera transaction around an example anchor; `Closer` moves in another 0.3 m.
The anchor and flythrough route are specific to the example world; adapt them for your own world.
`Fly route` calculates and dispatches camera poses on the UI thread through Reanimated, with no JS callback per frame.
Reanimated and Worklets are dependencies of this app only.
The SDK draws no walking UI of its own.

Inside this monorepo the app installs [`@splatkit/react-native`](../../packages/react-native-splatkit/README.md) from `../../packages`, and builds the Android SDK from source, so the example always exercises the current API.
Outside it, install `@splatkit/react-native@next` and remove this repository's `postinstall`, `prestart`, `patch:tooling` and `check:tooling` scripts.

Linking the package rather than unpacking it costs the example one extra piece of Metro config, in [`metro.config.js`](metro.config.js).
The package keeps React and React Native as devDependencies, so from the linked directory Metro resolves them to the package's own `node_modules` and the bundle ends up with two copies of each.
Two copies of React Native means two view config registries, and `SplatKitView` registers in the one the renderer does not read, which fails at render with `View config getter callback for component 'SplatKitView' must be a function`.
The `resolveRequest` hook pins both module names to this app.
An app that installs the package from npm needs none of this.

## Setup from zero in your own app

```sh
npx @react-native-community/cli@latest init MyApp --version 0.87.1
cd MyApp
npm install @splatkit/react-native@next
```

Then match what this app changes from the template:

- `android/build.gradle`: `minSdkVersion = 29`.
- `android/gradle.properties`: `reactNativeArchitectures=arm64-v8a`.
- `ios/Podfile`: `platform :ios, '17.0'`, and the Xcode deployment target 17.0; then `cd ios && pod install`.
- `ios/SplatKitExample/AppDelegate.swift` and `Info.plist`: a scene delegate, which iOS 26 requires.
- Absolute paths for `withWorld` and the `collider` prop; here `MainActivity.kt` and `SceneDelegate` pass the directory holding them as the `filesPath` initial prop.
- [`App.tsx`](App.tsx): build the configuration, mount `SplatKitView` and rebuild when `onCapabilities` arrives.

## Run this app

Needs Node 22.13 or newer, the Android SDK with NDK, Xcode 26 and CocoaPods.
Worlds are not committed; [Preparing a world](../../packages/react-native-splatkit/README.md#preparing-a-world) turns a PLY into the `.lodsplat` and the collider:

```sh
../../scripts/prepare-world.sh scene.ply out/ --sh 1 --lod --collider
```

```sh
npm ci
```

`postinstall` applies the development-tool [braces depth guard](../../packages/react-native-splatkit/scripts/patches/braces-3.0.3/validate-depth.js).
After `npm ci --ignore-scripts`, run `npm run patch:tooling`; `npm run check:tooling` verifies the patch and glob regressions.
The upstream version remains `3.0.3`, so `npm audit` still lists the advisory.

Android, on an arm64 device with Vulkan 1.1:

```sh
npm run android
files=/sdcard/Android/data/com.splatkit.example/files
adb push out/scene.lodsplat $files/world.lodsplat
adb push out/scene.collider.glb $files/collider.glb
```

iOS, on a device with iOS 17 or newer; set your signing team in Xcode first:

```sh
cd ios && pod install && cd ..
npm run ios -- --device
copy() { xcrun devicectl device copy to --device <device-id> --domain-type appDataContainer \
  --domain-identifier com.splatkit.example --source "$1" --destination "Documents/$2"; }
copy out/scene.lodsplat world.lodsplat
copy out/scene.collider.glb collider.glb
```

A missing world file fails the load and names the file; restart the app after replacing one.
The collider is optional: without one the world still renders and looks around, and the thumb stick stays hidden.
The status line shows the load, drawn splats and GPU time, or why the world failed to load.

The iOS scripts reuse `ios/build/dd` across sessions and build only the active architecture for local runs.
`npm run ios:build` compiles without launching; pass `--mode Debug` when needed.
Serialize builds sharing this directory; use a separate `--buildFolder` for concurrent or clean-build verification.
Distribution builds use the standard Xcode architecture settings.

## Camera verification

On a loaded world, compare `Fly route` with and without a deliberate JS-thread stall in a debug build.
For example, schedule `setTimeout(() => { const end = Date.now() + 3000; while (Date.now() < end) {} }, 1000)` from the debugger.
Record camera motion during the stall, not just renderer FPS; a static frame does not prove command delivery.
Also check Orbit, Closer, touch interruption, world replacement, and return to Walk.
Report device, build, world, and frame timings; simulator results do not establish physical GPU performance.
