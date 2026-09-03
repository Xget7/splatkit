# Contributing

SplatKit is experimental and small enough to read in an afternoon.
Pull requests are welcome; `docs/ROADMAP.md` lists what is open and how each item is proven.
Open an issue before starting anything larger than a fix so the scope is agreed first.

## Build the core

```
cd packages/splat-core
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

`cmake -DSPLAT_CORE_SANITIZE=thread` or `address` builds the sanitized variants CI runs.
Set `SPLAT_FIXTURES_DIR` to a folder with World Labs example files to run the integration tests.

## Build and run the Android dev app

Needs Android Studio with NDK 27.1.12297006, an arm64 device or emulator, and a `.spz` plus its collider `.glb` in `apps/android-dev/app/src/main/assets`.

```
scripts/fetch-validation-layers.sh
cd apps/android-dev
./gradlew :app:installDebug
adb logcat -s SplatKit
```

Debug builds load the Khronos validation layer; a pull request must leave it silent.

## What a pull request needs

- Tests in `splat-core` for anything that touches decoding, sorting, math or navigation.
- A note in the description saying which device and driver it was tried on.
  Emulator only is fine for logic; renderer changes need a real GPU.
- No planning documents: architecture decisions go in `docs/adr`, everything else in the pull request text.
- Plain dashes, no em dashes, one sentence per line in Markdown.

## Code layout

`packages/splat-core` has no graphics dependency and is shared by every engine.
`packages/splatkit-android` owns everything Vulkan and Android.
The engine does not know React Native exists.
