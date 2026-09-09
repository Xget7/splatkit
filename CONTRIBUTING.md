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

## Lint the C++

```
scripts/lint-cpp.sh          # what CI runs: clang-format check, then clang-tidy
scripts/lint-cpp.sh --fix    # rewrite the formatting and apply the fixes clang-tidy can
```

Both tools come from the NDK the project pins, so a machine that builds the library can lint it; `.clang-format` and `.clang-tidy` at the root hold the rules.
The script configures both packages for the Android target and lints tests and tools too.

## What a pull request needs

- Tests in `splat-core` for anything that touches decoding, sorting, math, navigation, loading or the visibility policy.
- `scripts/lint-cpp.sh` clean; CI runs it.
- A note in the description saying which device and driver it was tried on.
  Emulator only is fine for logic; renderer changes need a real GPU.
- No planning documents: architecture decisions go in `docs/adr`, everything else in the pull request text.
- Plain dashes, no em dashes, one sentence per line in Markdown.

## Code layout

`packages/splat-core` has no graphics dependency and is shared by every engine: formats, sorting, the level of detail tree, navigation, file mapping, the world loader and the visibility policy, each with tests.
`packages/splatkit-android` owns everything Vulkan and Android.
Its C++ is one `Engine` that owns a `SurfaceRenderer` (surface, swapchain, pipelines, the world on the GPU), the camera, the sorter, a `Benchmark` and a `StatsPublisher`; `jni/` is the boundary to Kotlin and knows nothing else.
Its Kotlin has three layers: `com.splatkit` is the public API (`SplatSurfaceView` and the value types), `com.splatkit.engine` the JNI boundary and the render thread, `com.splatkit.input` touch and the gyroscope.
The engine does not know what is hosting it; [ADR 0013](docs/adr/0013-engine-modules.md) records the split.
