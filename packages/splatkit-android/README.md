# splatkit-android

Android engine for Gaussian splat worlds: Vulkan renderer, walk and fly camera, touch and gyroscope input, and a `SurfaceView` to put in a layout.
Uses shared `splatkit-engine`/`splat-core`.
`apps/android-dev` is a plain Android host that uses everything below.

Requirements: Android 10 (API 29) and a Vulkan 1.1 device.
Only `arm64-v8a` is built.

## Use it

```kotlin
dependencies {
    implementation("io.github.xget7:splatkit-android:0.1.0-alpha04")
}
```

The AAR ships `arm64-v8a` only.
A build that also targets `x86_64` still compiles, but the library cannot load on an x86_64 emulator; develop on a physical arm64 device.

To work against a checkout instead, include the module in `settings.gradle.kts`:

```kotlin
include(":splatkit-android")
project(":splatkit-android").projectDir = file("../splatkit/packages/splatkit-android")
```

and depend on it with `implementation(project(":splatkit-android"))`.

Put a `SplatSurfaceView` in the layout, forward the lifecycle, and hand it the bytes of an SPZ file:

```kotlin
class WorldActivity : Activity() {
    private lateinit var splatView: SplatSurfaceView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        splatView = SplatSurfaceView(this)
        setContentView(splatView)
        splatView.listener = object : SplatSurfaceView.Listener {
            override fun onWorldReady(splatCount: Int) { /* hide the spinner */ }
            override fun onWorldFailed(message: String) { /* show it */ }
        }
        Thread {
            splatView.loadWorld(assets.open("world.spz").use { it.readBytes() })
            splatView.loadCollider(assets.open("collider.glb").use { it.readBytes() })
        }.start()
        splatView.setMotionEnabled(true)
    }

    override fun onResume() { super.onResume(); splatView.resume() }
    override fun onPause() { splatView.pause(); super.onPause() }
    override fun onDestroy() { splatView.release(); super.onDestroy() }
}
```

`loadWorld` returns immediately and decodes on the engine's loader thread; reading the bytes is what needs a background thread, as above.
The upload to the GPU happens on the next frame and `onWorldReady` follows on the main thread.
Without a collider the camera flies; with one it walks on the mesh and `onColliderReady` fires.
World Labs exports both files for every world.

## API

| Member | What it does |
|---|---|
| `loadWorld(bytes)`, `loadWorld(file)` | Decodes a splat file (SPZ versions 2 to 4 today; the format is detected from the bytes) and replaces the current world when ready. The `File` overload maps the file instead of copying it through the Java heap: use it for anything big. |
| `loadCollider(bytes)`, `loadCollider(file)` | Decodes a GLB mesh and switches to walk mode. |
| `cameraPose` | Position in meters plus yaw and pitch in radians, as of the last frame. Set it to teleport or to restore a saved viewpoint; when walking the camera settles on the floor under the new point. |
| `listener` | `Listener` with `onWorldReady`, `onWorldFailed`, `onColliderReady`, `onColliderFailed`, on the main thread. |
| `isAvailable` | False when Vulkan could not start; the view stays blank and every call is a no-op. |
| `applyQuality(RenderQuality)` | Sets the five values below from a preset: `RenderQuality.LOW`, `MEDIUM`, `HIGH` (the default) or `ULTRA`, or a `copy` of one. Each preset's reason is on the class and its numbers are in `docs/BENCHMARKS.md`. |
| `renderScale` | Fraction of the surface resolution the splats are drawn at, 0.1 to 2, then rescaled. 0.7 is hard to tell from 1.0 and much cheaper; above 1 supersamples, for the square of the scale in frame time. |
| `cullMarginDegrees` | Angular margin around the view kept drawn so a turn never meets an empty edge; 10 by default, widened further by the engine during a fast turn. |
| `linearBlending` | Blend in linear light instead of the encoded space the training used. Richer contrast the training never saw, 40% of the frame on Adreno 640. Off by default. |
| `splatBudget` | Most splats drawn per frame through a level of detail tree, 0 (default) draws them all. For scenes far bigger than the view or for low quality modes; at full resolution on a 2M scene it saves nothing and softens the image. Applies to worlds loaded after it is set. |
| `shDegree` | Spherical harmonics degree drawn, 0 to 3, capped by what the world carries; takes effect on the next frame. The harmonics are the view dependent colour: the glint on water and leaves. Free to draw on Adreno 640. |
| `maxShDegree` | Highest degree kept in GPU memory from the file, for worlds loaded after it is set. A memory cap, 92 bytes per splat at degree 3, not a quality setting; `shDegree` cannot exceed it for that world. World Labs worlds carry none. |
| `setMotionEnabled(bool)` | The gyroscope drives the look direction. |
| `setWalkVelocity(forward, right)` | Continuous walking in meters per second, for an on screen joystick. |
| `lookSensitivity`, `walkSensitivity` | Gesture tuning: one finger looks, two fingers walk, double tap toggles the gyroscope. |
| `readStats()` | fps, frame and GPU milliseconds, sort time, splat count. Cheap, any thread. |
| `gpuDescription` | GPU name and Vulkan version from the driver. |
| `startBenchmark(seconds)` | A reproducible turn with the frame time distribution in logcat, tag `SplatKit`. |

Quality presets, measured on the Mi 9 with the 2M splat World Labs house at 1080x2261:

| Preset | Render scale | Harmonics | Budget | Cull margin | Why | House GPU ms p50 |
|---|---|---|---|---|---|---|
| `LOW` | 0.5 | 0 | 500k | 10 | Phones that cannot hold 30 fps at medium, or battery: half the pixels, base colour only, and a level of detail budget so a scene of any size costs about the same. | 12.4 |
| `MEDIUM` | 0.7 | 1 | all | 10 | 60 fps on the Mi 9: 0.7 is hard to tell from 1.0 at arm's length, degree 1 keeps the broad view dependent tint for a fifth of the harmonics work. | 13.4 |
| `HIGH` | 1.0 | 3 | all | 10 | The default: every pixel, every splat, every harmonic, what the reference rasterizer draws. | 19.3 |
| `ULTRA` | 1.5 | 3 | all | 20 | Flagship GPUs and stills: supersampling settles the thin splats that shimmer at a pixel each, and a flick never shows an empty edge. | 39.1 |

### Hosting it elsewhere

`SplatSurfaceView` is a plain `SurfaceView`, so any host that can show an Android view can show it.
Jetpack Compose:

```kotlin
AndroidView(
    factory = { context -> SplatSurfaceView(context).also { view = it } },
    modifier = Modifier.fillMaxSize(),
)
```

and forward `resume`, `pause` and `release` from a `DisposableEffect` on the lifecycle.
A React Native or Flutter view manager wraps it the same way: create the view, map props to the properties above, map commands to `loadWorld`, `cameraPose` and `setWalkVelocity`, and turn `Listener` calls into events.
Everything on the view is safe to call from the main thread; loads run on the library's own loader thread and settings are posted to the render thread.

`com.splatkit.ui` has `SplatHudView` (the stats overlay) and `JoystickView`, both optional.

The engine draws only when the camera, the world or the surface changed, so a still scene costs no GPU time.

Declare `android:appCategory="game"` in the host manifest: Android's power HAL keys its game performance mode on it, and Xiaomi's Game Turbo lists such apps.
Thread priority and big core affinity for the engine threads were measured on the Mi 9 and changed nothing (see the roadmap), so the library does not set them.

`readStats()` exposes source `splatCount`/`loadedSplatCount`, completed `drawnSplatCount`, and compute/nonempty/hardware screen-tile counts.
Snapshots can lag; zero drawn counts never imply the source count.
Float transport preserves every integer through 16,777,216; larger counts may round.
[iOS SwiftPM alpha](https://github.com/Xget7/splatkit-ios) is available; RN GPU options remain pending.

Decoder tests: run `./gradlew :splatkit:testDebugUnitTest` from `apps/android-dev`.

## Logs

Everything logs under the tag `SplatKit`.
MIUI hides application logs until `adb shell setprop persist.log.tag.SplatKit V`.

## Layout

`rendering/vulkan`: GPU; `engine/AndroidEngine`: host; `jni`: boundary; `splatkit-engine`: orchestration; `splat-core`: algorithms.
Current source integrates GPU LOD, visibility, stable radix and indirect drawing; Maven alpha04 predates it.
Android arm64/Mac-GPU emulator checks pass; physical Android and image-quality acceptance remain pending.
Contracts: [Vulkan](docs/VULKAN.md).
