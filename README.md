# SplatKit

[![Maven Central](https://img.shields.io/maven-central/v/io.github.xget7/splatkit-android?label=Maven%20Central)](https://central.sonatype.com/artifact/io.github.xget7/splatkit-android)
[![splatkit-android](https://github.com/Xget7/splatkit-android/actions/workflows/android.yml/badge.svg)](https://github.com/Xget7/splatkit-android/actions/workflows/android.yml)
[![splat-core](https://github.com/Xget7/splatkit-android/actions/workflows/core.yml/badge.svg)](https://github.com/Xget7/splatkit-android/actions/workflows/core.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

Real-time Gaussian splatting engine for Android on Vulkan: SPZ scenes, CPU sort and cull, spherical harmonics, level of detail, and walk navigation with colliders.
SplatKit loads a 3D Gaussian splat scene, such as a World Labs Marble world, and lets the user walk through it with touch, an on screen joystick or the phone's gyroscope.
It ships as a single Android view that you drop into any layout, Compose tree or cross platform view manager.

```kotlin
implementation("io.github.xget7:splatkit-android:0.1.0-alpha04")
```

## Table of contents

- [Features](#features)
- [Requirements](#requirements)
- [Installation](#installation)
- [Quick start](#quick-start)
- [Configuration](#configuration)
- [How it works](#how-it-works)
- [Performance](#performance)
- [Project status and roadmap](#project-status-and-roadmap)
- [Documentation](#documentation)
- [Contributing](#contributing)
- [License](#license)

## Features

- **Walkable scenes.**
  Load a splat world together with its collider mesh and the camera walks on the floor, climbs stairs and stops at walls.
  Without a collider the camera flies freely.
- **Vulkan renderer.**
  The splats are sorted back to front, culled against the view and drawn as blended quads through a Vulkan 1.1 pipeline written for mobile GPUs.
- **Input built in.**
  One finger looks around, two fingers walk, double tap toggles the gyroscope.
  A joystick view and a stats overlay are included and optional.
- **Quality presets.**
  Four presets from `LOW` to `ULTRA` trade resolution, view dependent colour and splat budget against frame time.
  Every value behind a preset can be adjusted on its own.
- **Measured, not guessed.**
  Every performance claim in this repository comes with the device, the scene and the number that produced it.
  The full log lives in [docs/BENCHMARKS.md](docs/BENCHMARKS.md).
- **Small, dependency free core.**
  Format decoding, sorting, math and navigation live in a C++17 library with no graphics or platform code, so a second renderer can reuse them unchanged.
- **No app, no account, no cloud.**
  MIT licensed source you can read, change, build and measure on your own hardware.

## Requirements

| | Minimum |
|---|---|
| Android | 10 (API 29) |
| GPU | Vulkan 1.1 capable |
| ABI | `arm64-v8a` only |
| Input format | SPZ versions 2 to 4, GLB for colliders |

The AAR does not include x86 or x86_64 binaries, so the view cannot load on the Android emulator.
Develop and test on a physical arm64 device.

## Installation

Add the dependency to your module's `build.gradle.kts`:

```kotlin
dependencies {
    implementation("io.github.xget7:splatkit-android:0.1.0-alpha04")
}
```

The library is published on Maven Central, so no extra repository is needed.
It ships its own ProGuard consumer rules; nothing has to be added for R8.

Declare the game category in your app's manifest so Android's power management and vendor game modes treat the app as a game:

```xml
<application android:appCategory="game" ...>
```

To build against a local checkout instead of the published artifact, see the [library README](packages/splatkit-android/README.md#use-it).

## Quick start

Put a `SplatSurfaceView` on screen, forward the activity lifecycle, and hand it the bytes of an SPZ file.
World Labs exports both the `.spz` world and the `.glb` collider for every scene.

```kotlin
class WorldActivity : Activity() {
    private lateinit var splatView: SplatSurfaceView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        splatView = SplatSurfaceView(this)
        setContentView(splatView)

        splatView.listener = object : SplatSurfaceView.Listener {
            override fun onWorldReady(splatCount: Int) { /* hide the spinner */ }
            override fun onWorldFailed(message: String) { /* show the error */ }
        }

        // Reading the file is the only part that needs a background thread.
        // Decoding and GPU upload run on the engine's own threads.
        Thread {
            splatView.loadWorld(assets.open("world.spz").use { it.readBytes() })
            splatView.loadCollider(assets.open("collider.glb").use { it.readBytes() })
        }.start()

        splatView.setMotionEnabled(true) // gyroscope drives the look direction
    }

    override fun onResume() { super.onResume(); splatView.resume() }
    override fun onPause() { splatView.pause(); super.onPause() }
    override fun onDestroy() { splatView.release(); super.onDestroy() }
}
```

`onWorldReady` fires on the main thread once the world is on the GPU.
With a collider loaded the camera switches to walk mode and `onColliderReady` fires.

### Jetpack Compose

`SplatSurfaceView` is a plain `SurfaceView`, so it goes inside an `AndroidView` and gets `resume`, `pause` and `release` from a `DisposableEffect` on the lifecycle:

```kotlin
AndroidView(
    factory = { context -> SplatSurfaceView(context).also { view = it } },
    modifier = Modifier.fillMaxSize(),
)
```

### React Native, Flutter and other hosts

A view manager wraps the same view: create it, map props to the properties below, map commands to `loadWorld`, `cameraPose` and `setWalkVelocity`, and turn `Listener` calls into events.
Everything on the view is safe to call from the main thread.

## Configuration

Pick a preset with `applyQuality(RenderQuality.MEDIUM)`, or take a preset and change one value with `copy`.
The default is `HIGH`.

| Preset | Render scale | Harmonics degree | Splat budget | Intended for |
|---|---|---|---|---|
| `LOW` | 0.5 | 0 | 500k | Phones that cannot hold 30 fps at medium, or battery saving |
| `MEDIUM` | 0.7 | 1 | all | 60 fps on a 2019 flagship, hard to tell from full resolution at arm's length |
| `HIGH` | 1.0 | 3 | all | Every pixel, every splat, every harmonic: what the reference rasterizer draws |
| `ULTRA` | 1.5 | 3 | all | Supersampling for flagship GPUs and still captures |

The individual settings are also available on the view:

| Property | What it controls |
|---|---|
| `renderScale` | Fraction of the surface resolution the splats are drawn at, 0.1 to 2. |
| `shDegree` | Spherical harmonics degree drawn, 0 to 3. These carry the view dependent colour, such as glints on water and leaves. |
| `maxShDegree` | Highest harmonics degree kept in GPU memory. A memory cap, not a quality setting. |
| `splatBudget` | Most splats drawn per frame through a level of detail tree. 0 draws them all. |
| `cullMarginDegrees` | Angular margin kept drawn around the view so a fast turn never shows an empty edge. |
| `linearBlending` | Blend in linear light instead of the encoded space the scene was trained in. Off by default. |
| `lookSensitivity`, `walkSensitivity` | Gesture tuning. |
| `cameraPose` | Read or set the camera position and orientation, for teleporting or restoring a viewpoint. |

`readStats()` returns frames per second, frame and GPU milliseconds, sort time and splat count from any thread.
`startBenchmark(seconds)` runs a reproducible turn in place and logs the frame time distribution.
The [library README](packages/splatkit-android/README.md#api) documents every member.

## How it works

The repository is a monorepo with two packages and one app:

```
packages/splat-core/          C++17 core: formats, sorting, math, navigation. No graphics, no platform code.
packages/splatkit-android/    Android library: Vulkan renderer, camera, input, SplatSurfaceView.
apps/android-dev/             Development and benchmark app.
docs/adr/                     Architecture decision records, one file per decision.
docs/BENCHMARKS.md            Every measurement, with device, commit and settings.
docs/ROADMAP.md               What works, what is next, what is open, what is deferred and why.
```

**splat-core** decodes SPZ and GLB files into a single internal coordinate frame, orders the splats spatially at load time, sorts them back to front on a background thread while the camera moves, and provides the collider grid, raycasts and character controller that make a scene walkable.
It has no graphics dependency and no host framework in it.
The same core can sit under a Metal or WebGPU engine without touching the formats, the sorting or the navigation.

**splatkit-android** owns the Vulkan context, swapchain and frame loop.
The engine sorts on movement and culls on rotation, blends in the encoded colour space the scene was trained in, and draws only when the camera, the world or the surface has changed, so a still scene costs no GPU time.
A Choreographer driven render thread and a JNI boundary connect it to the Kotlin view.

Every non obvious choice is written down in [docs/adr](docs/adr), from why the project is Android first and Vulkan direct to why blending happens in the encoded space and why the CPU sort is the baseline.

## Performance

Measured on a Xiaomi Mi 9 (Adreno 640, Vulkan 1.1, Android 11), release build, phone cooled below 48 C before each run.

| Scene | Splats | Preset | GPU ms p50 |
|---|---|---|---|
| World Labs kitchen | 500k | `HIGH` (scale 1.0) | 14.0 |
| World Labs house | 2M | `HIGH` (scale 1.0) | 19.4 |
| World Labs house | 2M | `MEDIUM` (scale 0.7) | 13.6, holds 60 fps |
| World Labs house | 2M | `LOW` | 12.4 |
| World Labs house | 2M | `ULTRA` | 39.1 |

Every optimisation in the engine started with a measurement before it and ended with the same measurement after.
Attempts that did not pay off are recorded too, with their numbers, so nobody has to repeat them.
See [docs/BENCHMARKS.md](docs/BENCHMARKS.md) for the full log and the image quality comparison against the reference renderer.

## Project status and roadmap

**Experimental, pre alpha.**
The API will change until 0.1.0.

Works today:

- World Labs worlds in SPZ versions 2 to 4, with walk mode from the exported GLB collider.
- Touch, joystick and gyroscope input.
- Four quality presets, level of detail budget, frustum culling and spherical harmonics up to degree 3.
- Published AAR on Maven Central with CI on every pull request.

Open for contribution, each with a stated proof of success in [docs/ROADMAP.md](docs/ROADMAP.md):

- PLY, `.splat` and SOG input formats.
  Until then the `ply2spz` tool in `splat-core` converts a PLY offline.
- GPU sorting behind a feature flag.
- Fewer blended fragments, the main cost on Adreno.
- Validation on Mali GPUs (Samsung, Pixel).
- Swapchain pre-rotation, chunked world upload, persisted pipeline cache.

Not planned inside this engine: iOS, macOS or visionOS, and host framework bindings.
The core is written so that a Metal engine and a React Native or Flutter binding can each live in their own package.

## Documentation

| Document | What it covers |
|---|---|
| [packages/splatkit-android/README.md](packages/splatkit-android/README.md) | Full API reference, hosting in Compose and cross platform frameworks, logging, internal layout |
| [packages/splat-core/README.md](packages/splat-core/README.md) | Core domains, building and testing, converting PLY files |
| [docs/adr](docs/adr) | Architecture decision records |
| [docs/BENCHMARKS.md](docs/BENCHMARKS.md) | Benchmark log and image quality comparison |
| [docs/ROADMAP.md](docs/ROADMAP.md) | Status, next steps, open items, deferred items |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Building the core, running the dev app, linting, what a pull request needs |

## Contributing

Contributions are welcome.
Most open items need a real device more than deep Vulkan knowledge: running the dev app on a Mali phone and attaching logs is already a valuable report.

1. Read [CONTRIBUTING.md](CONTRIBUTING.md) for how to build the core, run the dev app and lint the C++.
2. Pick an item from [docs/ROADMAP.md](docs/ROADMAP.md).
   Each one says what it touches and how to prove it works.
3. Open a pull request with the measurement that backs the change.
   A performance change without a number from a device will be asked for one.

Bug reports with a crash log or a black screen and the device model are equally welcome.

## License

SplatKit is released under the [MIT License](LICENSE).
Third party licenses are listed in [THIRD_PARTY_LICENSES.txt](THIRD_PARTY_LICENSES.txt).
