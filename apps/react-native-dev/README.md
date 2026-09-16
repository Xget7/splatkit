# SplatKit RN Dev

Minimal React Native 0.87.1 Android app (New Architecture, Hermes) that mounts `@splatkit/react-native` from `packages/react-native-splatkit`.
It exercises the Fabric path end to end: JS props and Codegen, the Kotlin adapter, JNI and Vulkan.
Only `arm64-v8a` is built; the reference device is a Xiaomi Mi 9 (Android 11, Adreno 640).

## Build

Needs the Node version in `package.json`, and the Android SDK with NDK `27.1.12297006`.

```sh
npm ci
cd android
./gradlew :app:assembleRelease
```

The release APK is debug-signed, unminified and embeds the JS bundle, so it runs without Metro.
Release lint-vital is off because AGP 9.2.1 lint crashes analyzing `MainActivity.kt`.

## Run

```sh
adb install -r android/app/build/outputs/apk/release/app-release.apk
adb shell mkdir -p /sdcard/Android/data/com.splatkit.rndev/files
adb push ../android-dev/app/src/main/assets/kitchen_500k.spz /sdcard/Android/data/com.splatkit.rndev/files/world.spz
adb shell am start -n com.splatkit.rndev/.MainActivity
adb logcat -v threadtime -s ReactNativeJS SplatKit
```

The world path is the app's own external-files directory, so it needs no storage permission.
`ReactNativeJS` lines prefixed `[SplatKitRN]` log every native event; `SplatKit` is the native tag.
Preset buttons change splat budgets and so reload the world together with a new policy revision.
`sortDepth` and `subpixel` change only the policy; `Pause`, `Reload world` and `Remove world` exercise lifecycle.
The policy revision advances only when the built policy changes, in the same render as the props that changed it.
The overlay shows the last world, stats, capabilities and policy events, and the builder's diagnostics.
`settings.gradle` builds the adapter from its real path, because Kotlin incremental compilation fails through the `node_modules` symlink.
