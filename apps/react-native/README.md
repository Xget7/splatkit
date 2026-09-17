# SplatKit React Native example

A React Native 0.87.1 app, made with `npx @react-native-community/cli init`, that installs [`@splatkit/react-native`](../../packages/react-native-splatkit/README.md) from npm and renders one full-screen `SplatKitView`.
Drag with one finger to look around and with two fingers to walk.

## Setup from zero in your own app

```sh
npx @react-native-community/cli@latest init MyApp --version 0.87.1
cd MyApp
npm install @splatkit/react-native
```

Then match what this app changes from the template:

- `android/build.gradle`: `minSdkVersion = 29`.
- `android/gradle.properties`: `reactNativeArchitectures=arm64-v8a`.
- `ios/Podfile`: `platform :ios, '17.0'`, and the Xcode deployment target 17.0; then `cd ios && pod install`.
- `ios/SplatKitExample/AppDelegate.swift` and `Info.plist`: a scene delegate, which iOS 26 requires.
- A world file path for `withWorld`; here `MainActivity.kt` and `SceneDelegate` pass it as the `worldPath` initial prop.
- [`App.tsx`](App.tsx): build the configuration, mount `SplatKitView` and rebuild when `onCapabilities` arrives.

## Run this app

Needs Node 22.11 or newer, the Android SDK with NDK, Xcode 26 and CocoaPods.
Worlds are `.spz`, `.ply` or `.lodsplat` files and are not committed.

```sh
npm ci
```

Android, on an arm64 device with Vulkan 1.1:

```sh
npm run android
adb push world.spz /sdcard/Android/data/com.splatkit.example/files/world.spz
```

iOS, on a device with iOS 17 or newer; set your signing team in Xcode first:

```sh
cd ios && pod install && cd ..
npm run ios -- --device
xcrun devicectl device copy to --device <device-id> --domain-type appDataContainer \
  --domain-identifier com.splatkit.example --source world.spz --destination Documents/world.spz
```

Restart the app after copying a world.
The status line shows the load, drawn splats and GPU time, or why the world failed to load.
