# splatkit-android

Android engine for Gaussian splat worlds: Vulkan renderer, walk and fly camera, touch and gyroscope input, and a `SurfaceView` to put in a layout.
Consumes `splat-core` for formats, sorting and navigation.
No React Native dependency; `apps/android-dev` is a plain Android host that uses everything below.

Requirements: Android 10 (API 29) and a Vulkan 1.1 device.
Only `arm64-v8a` is built.

## Use it

```kotlin
dependencies {
    implementation("io.github.xget7:splatkit-android:0.1.0-alpha01")
}
```

The AAR ships `arm64-v8a` only.
A build that also targets `x86_64` still compiles, but the library cannot load on an x86_64 emulator; develop on a physical arm64 device.

To work against a checkout instead, include the module in `settings.gradle.kts`:

```kotlin
include(":splatkit-android")
project(":splatkit-android").projectDir = file("../react-native-splat/packages/splatkit-android")
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

`loadWorld` decodes on the calling thread, so call it from a background thread; the upload to the GPU happens on the next frame and `onWorldReady` follows on the main thread.
Without a collider the camera flies; with one it walks on the mesh and `onColliderReady` fires.
World Labs exports both files for every world.

## API

| Member | What it does |
|---|---|
| `loadWorld(bytes)` | Decodes a splat file (SPZ versions 2 to 4 today; the format is detected from the bytes) and replaces the current world when ready. |
| `loadCollider(bytes)` | Decodes a GLB mesh and switches to walk mode. |
| `listener` | `Listener` with `onWorldReady`, `onWorldFailed`, `onColliderReady`, `onColliderFailed`, on the main thread. |
| `isAvailable` | False when Vulkan could not start; the view stays blank and every call is a no-op. |
| `renderScale` | Fraction of the surface resolution the splats are drawn at, then upscaled. 0.7 is hard to tell from 1.0 and much cheaper. |
| `maxShDegree` | Highest spherical harmonics degree kept from the file, 0 to 3, for worlds loaded after it is set. Degree 3 costs 92 bytes per splat of GPU memory. World Labs worlds carry none. |
| `setMotionEnabled(bool)` | The gyroscope drives the look direction. |
| `setWalkVelocity(forward, right)` | Continuous walking in meters per second, for an on screen joystick. |
| `lookSensitivity`, `walkSensitivity` | Gesture tuning: one finger looks, two fingers walk, double tap toggles the gyroscope. |
| `readStats()` | fps, frame and GPU milliseconds, sort time, splat count. Cheap, any thread. |
| `gpuDescription` | GPU name and Vulkan version from the driver. |
| `startBenchmark(seconds)` | A reproducible turn with the frame time distribution in logcat, tag `SplatKit`. |

`com.splatkit.ui` has `SplatHudView` (the stats overlay) and `JoystickView`, both optional.

The engine draws only when the camera, the world or the surface changed, so a still scene costs no GPU time.

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
