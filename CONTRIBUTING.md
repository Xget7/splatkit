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

## Build and run the iOS dev app

Needs Xcode 26 with the iOS platform and the Metal toolchain, CMake, [xcodegen](https://github.com/yonaskolb/XcodeGen), an iPhone, and a `.spz` plus its collider `.glb` in `apps/ios-dev/SplatKitDev/Resources`.

```
scripts/build-ios.sh                      # the static libraries, into build/ios/lib
cd apps/ios-dev
xcodegen generate
xcodebuild -scheme SplatKitDev -configuration Release -destination "id=<device udid>" -allowProvisioningUpdates build
xcrun devicectl device install app --device <udid> build/Build/Products/Release-iphoneos/SplatKitDev.app
xcrun devicectl device process launch --device <udid> --console com.splatkit.devapp -- --gyro 0
```

The app reads its switches from the command line (`--world`, `--tileset`, `--collider`, `--residency`, `--scale`, `--pose`, `--benchmark`, `--capture`; see `LaunchArgs.swift`) and worlds from its Documents folder, which `devicectl device copy to` fills.
The Objective-C++ sources build with warnings as errors; `scripts/lint-cpp.sh` formats them.

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
`packages/splatkit-engine` is the engine without a graphics API: one `SplatEngine` that owns the camera, the sorter or the streamer, a `Benchmark` and a `StatsPublisher`, and draws through the `SplatRenderer` interface; the GPU record layout (`GpuSplat` and the packing) lives here so every renderer uploads the same bytes.
`packages/splatkit-android` owns everything Vulkan and Android: `VulkanSplatRenderer` (surface, swapchain, pipelines, the world on the GPU) implements the interface, `AndroidEngine` wires it under the engine, and `jni/` is the boundary to Kotlin and knows nothing else.
`packages/splatkit-ios` owns everything Metal and iOS: `MetalSplatRenderer` implements the interface over a `CAMetalLayer`, `SKSplatEngine` is the Objective-C boundary to Swift, and `Sources/SplatKit` is the Swift layer (render thread, motion, `SplatMetalView`).
Its Kotlin has three layers: `com.splatkit` is the public API (`SplatSurfaceView` and the value types), `com.splatkit.engine` the JNI boundary and the render thread, `com.splatkit.input` touch and the gyroscope.
The engine does not know what is hosting it; [ADR 0013](docs/adr/0013-engine-modules.md) records the split.

## Names

The root object of each layer carries the domain in its name, so a reader who sees it anywhere knows what project it belongs to: `SplatEngine` in C++ and Kotlin, `VulkanSplatRenderer`, `SplatWorldLoader`, `SplatSurfaceView`, `SplatPipeline`.
The same thing has the same name on both sides of a boundary: the Kotlin `SplatEngine` wraps the C++ `SplatEngine`, and the JNI symbols are derived from that one name.
Objects below the root are named for their one job and live in the directory of that job, `rendering/vulkan`, `diagnostics`, `camera`, `jni`, `loading`, `sorting`; nothing sits loose at the root of `cpp/`.
A name that only says what something is, `Engine`, `Renderer`, `Loader`, is not enough; a name that repeats the directory, `VulkanVulkanContext`, is too much.
[ADR 0014](docs/adr/0014-names-carry-the-domain.md) records the rule.
