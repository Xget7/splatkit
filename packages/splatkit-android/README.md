# splatkit-android

Android engine for Gaussian splat worlds: Vulkan renderer, walk and fly camera, touch and gyroscope input, and a `SurfaceView` to put in a layout.
Consumes `splat-core` for formats, sorting and navigation.
`apps/android-dev` is a plain Android host that uses everything below.

Requirements: Android 10 (API 29) and a Vulkan 1.1 device.
Only `arm64-v8a` is built.

## Use it

```kotlin
dependencies {
    implementation("io.github.xget7:splatkit-android:0.1.0-alpha02")
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
| `loadWorld(bytes)` | Decodes a splat file (SPZ versions 2 to 4 today; the format is detected from the bytes) and replaces the current world when ready. |
| `loadCollider(bytes)` | Decodes a GLB mesh and switches to walk mode. |
| `listener` | `Listener` with `onWorldReady`, `onWorldFailed`, `onColliderReady`, `onColliderFailed`, on the main thread. |
| `isAvailable` | False when Vulkan could not start; the view stays blank and every call is a no-op. |
| `applyQuality(RenderQuality)` | Sets the five values below from a preset: `RenderQuality.LOW`, `MEDIUM`, `HIGH` (the default) or `ULTRA`, or a `copy` of one. Each preset's reason is on the class and its numbers are in `docs/BENCHMARKS.md`. |
| `renderScale` | Fraction of the surface resolution the splats are drawn at, 0.1 to 2, then rescaled. 0.7 is hard to tell from 1.0 and much cheaper; above 1 supersamples, for the square of the scale in frame time. |
| `cullMarginDegrees` | Angular margin around the view kept drawn so a turn never meets an empty edge; 10 by default, widened further by the engine during a fast turn. |
| `linearBlending` | Blend in linear light instead of the encoded space the training used. Richer contrast the training never saw, 40% of the frame on Adreno 640. Off by default. |
| `splatBudget` | Most splats drawn per frame through a level of detail tree, 0 (default) draws them all. For scenes far bigger than the view or for low quality modes; at full resolution on a 2M scene it saves nothing and softens the image. Applies to worlds loaded after it is set. |
| `maxShDegree` | Highest spherical harmonics degree kept from the file, 0 to 3, for worlds loaded after it is set. Degree 3 costs 92 bytes per splat of GPU memory. World Labs worlds carry none. |
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
| `MEDIUM` | 0.7 | 1 | all | 10 | 60 fps on the Mi 9: 0.7 is hard to tell from 1.0 at arm's length, degree 1 keeps the broad view dependent tint for a fifth of the harmonics memory. | 13.4 |
| `HIGH` | 1.0 | 3 | all | 10 | The default: every pixel, every splat, every harmonic, what the reference rasterizer draws. | 19.3 |
| `ULTRA` | 1.5 | 3 | all | 20 | Flagship GPUs and stills: supersampling settles the thin splats that shimmer at a pixel each, and a flick never shows an empty edge. | 39.1 |

`com.splatkit.ui` has `SplatHudView` (the stats overlay) and `JoystickView`, both optional.

The engine draws only when the camera, the world or the surface changed, so a still scene costs no GPU time.

Declare `android:appCategory="game"` in the host manifest: Android's power HAL keys its game performance mode on it, and Xiaomi's Game Turbo lists such apps.
Thread priority and big core affinity for the engine threads were measured on the Mi 9 and changed nothing (see the roadmap), so the library does not set them.

## Logs

Everything logs under the tag `SplatKit`.
MIUI hides application logs until `adb shell setprop persist.log.tag.SplatKit V`.

## Layout

| Domain | Where | Responsibility |
|---|---|---|
| Rendering | C++ `src/main/cpp/rendering` | Vulkan context, swapchain, frame loop, offscreen target, splat pipeline |
| Engine | C++ `src/main/cpp/Engine.*` | Owns the world, the camera and the sorter; the render loop |
| Camera | C++ `src/main/cpp/camera` | Walk and fly camera over the `splat-core` character controller |
| JNI | C++ `src/main/cpp/jni` | The boundary; events cross it through `NativeEngine.onNativeEvent` |
| View | Kotlin `SplatSurfaceView`, `RenderThread`, `MotionInput` | Surface lifecycle, Choreographer driven render thread, sensors |

Shaders in `src/main/cpp/shaders` compile to SPIR-V headers at build time.
