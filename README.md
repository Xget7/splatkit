# react-native-splat

Walkable Gaussian splat worlds (World Labs Marble `.spz`) in React Native, rendered natively.
Android first (Vulkan), iOS second (Metal).

## Layout

```
packages/splat-core/          C++17 core with no graphics dependency: Formats, Sorting, Math, Navigation.
packages/splatkit-android/    Android library: Vulkan renderer, camera, input, scene, view.
packages/splatkit-ios/        iOS library (reserved).
packages/react-native-splat/  React Native package built on Nitro Modules.
apps/android-dev/             Android app for development and benchmarks.
docs/adr/                     Architecture decision records.
docs/ROADMAP.md               What is done, what is next, what is deferred and why.
```

The engine does not know React Native exists.

## Status

Early. The Android engine renders and walks World Labs worlds on the emulator; real device numbers are pending.
See `docs/ROADMAP.md` for what is next and `docs/adr` for the decisions taken so far.

## License

MIT.
