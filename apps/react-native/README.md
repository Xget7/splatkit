# SplatKit React Native example

A React Native 0.87.1 app, made with `npx @react-native-community/cli init`, that renders one full-screen `SplatKitView` you can walk through.
Drag anywhere to look around, and double tap to toggle the gyroscope.
Once the collider is ready a thumb stick appears; it is this app's own control, in [`Joystick.tsx`](Joystick.tsx), and it drives the view through `SplatKitCommands.setWalkVelocity`.
The SDK draws no walking UI of its own.

Inside this monorepo the app installs [`@splatkit/react-native`](../../packages/react-native-splatkit/README.md) from `../../packages`, and builds the Android SDK from source, so the example always exercises the current API.
Outside it, `npm install @splatkit/react-native@next` is the only change.

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
- A world file path for `withWorld`, and a collider path for the `collider` prop; here `MainActivity.kt` and `SceneDelegate` pass both as the `worldPath` and `colliderPath` initial props.
- [`App.tsx`](App.tsx): build the configuration, mount `SplatKitView` and rebuild when `onCapabilities` arrives.

## Run this app

Needs Node 22.13 or newer, the Android SDK with NDK, Xcode 26 and CocoaPods.
Worlds are `.spz`, `.ply` or `.lodsplat` files and are not committed.

```sh
npm ci
```

Android, on an arm64 device with Vulkan 1.1:

```sh
npm run android
adb push world.spz /sdcard/Android/data/com.splatkit.example/files/world.spz
adb push collider.glb /sdcard/Android/data/com.splatkit.example/files/collider.glb
```

iOS, on a device with iOS 17 or newer; set your signing team in Xcode first:

```sh
cd ios && pod install && cd ..
npm run ios -- --device
xcrun devicectl device copy to --device <device-id> --domain-type appDataContainer \
  --domain-identifier com.splatkit.example --source world.spz --destination Documents/world.spz
xcrun devicectl device copy to --device <device-id> --domain-type appDataContainer \
  --domain-identifier com.splatkit.example --source collider.glb --destination Documents/collider.glb
```

Restart the app after copying a world.
The collider is optional: without one the world still renders and looks around, and the thumb stick stays hidden.
The status line shows the load, drawn splats and GPU time, or why the world failed to load.
