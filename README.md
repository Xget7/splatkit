# SplatKit

Walkable Gaussian splat worlds on Android, rendered with Vulkan.
Load a World Labs Marble `.spz`, put a `SplatSurfaceView` in a layout, and walk through the scene.

```kotlin
implementation("io.github.xget7:splatkit-android:0.1.0-alpha02")
```

`packages/splatkit-android/README.md` has the full API and a working activity.

## Why

Gaussian splat viewers on Android are closed apps.
This is an open engine instead: MIT, no app to install, no account, no cloud, and the Vulkan source is there to read, change and measure.
Anyone with an Android device can build it, change the renderer and prove the difference on their own hardware.
Every performance claim in `docs/ROADMAP.md` comes with the device and the number that produced it.

## Layout

```
packages/splat-core/          C++17 core with no graphics dependency: Formats, Sorting, Math, Navigation.
packages/splatkit-android/    Android library: Vulkan renderer, camera, input, scene, view.
apps/android-dev/             Android app for development and benchmarks.
docs/adr/                     Architecture decision records.
docs/ROADMAP.md               What is done, what is next, what is deferred and why.
```

The core has no graphics dependency and no host framework in it, so a second engine can sit beside the Vulkan one without touching the formats, the sorting or the navigation.

## Status

Experimental, pre alpha.
The engine renders and walks World Labs worlds, SPZ versions 2 to 4.
On a Xiaomi Mi 9 (Adreno 640) a 500k splat kitchen runs at 60 fps with render scale 0.7, and a 2M splat house at 60 fps with render scale 0.5; `docs/ROADMAP.md` has the measurements.
Only `arm64-v8a` is built, and the API will change until 0.1.0.

## Contributing

Contributions are welcome, and most of what is open needs a real device rather than deep Vulkan knowledge.
`docs/ROADMAP.md` lists what is open and what each item has to prove.
`CONTRIBUTING.md` says how to build the core, how to run the dev app, and what a pull request needs.

## License

MIT.
