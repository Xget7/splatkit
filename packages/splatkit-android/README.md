# splatkit-android

Native `SplatSurfaceView`, Vulkan renderer, walk/fly camera, touch and gyroscope input.
Requires Android 10/API 29, Vulkan 1.1 and arm64-v8a.
GPU ordering additionally requires supported compute subgroups and buffer limits.
An arm64 emulator can test functionality; it is not a phone performance measurement.

## Install

Maven Central `0.1.0-alpha07` adds the render policy, `onWorldFrameReady`, the budgeted `loadWorld` and 16 KB alignment; see the [changelog](../../CHANGELOG.md) and [releases](https://github.com/Xget7/splatkit-android/releases).
Avoid alpha05, which corrupts Adreno sorting.

```kotlin
dependencies {
    implementation("io.github.xget7:splatkit-android:0.1.0-alpha07")
}
```

For current source, use `implementation(project(":splatkit"))` in the included `apps/android-dev` host, or include `packages/splatkit-android` as a Gradle module.

## Host

```kotlin
import android.app.Activity
import android.os.Bundle
import com.splatkit.SplatSurfaceView
import java.io.File

class WorldActivity : Activity() {
    private lateinit var splats: SplatSurfaceView

    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        splats = SplatSurfaceView(this)
        setContentView(splats)
        splats.listener = object : SplatSurfaceView.Listener {
            override fun onWorldReady(splatCount: Int) { /* hide loading UI */ }
            override fun onWorldFailed(message: String) { /* show error */ }
        }
        // Put your world in app storage first. File loading avoids a Java-heap copy.
        splats.loadWorld(File(filesDir, "world.spz"))
    }

    override fun onResume() { super.onResume(); splats.resume() }
    override fun onPause() { splats.pause(); super.onPause() }
    override fun onDestroy() { splats.release(); super.onDestroy() }
}
```

Loads decode asynchronously; ready/failure callbacks run on the main thread.
`onWorldReady` means uploaded; `onWorldFrameReady` fires once after a world draw's Vulkan fence completes.
Keep the view resumed while waiting; completion does not prove visible pixels, presentation scanout or full streamed detail.
Use `loadCollider(File)` for optional GLB walk collision; otherwise the camera flies.
SPZ v2–v4 and offline `.lodsplat` worlds are supported, and `loadTiledWorld` streams a `splat-tile` tileset.
PLY needs offline conversion with `ply2spz`.
Compose can host the view through `AndroidView`; forward the same lifecycle.
The [React Native package](../react-native-splatkit/README.md) maps its `policy` prop to `applyRenderPolicy`.

## Controls

| API | Contract |
|---|---|
| `loadWorld(bytes/file)`, `loadCollider(bytes/file)` | Load asynchronously; prefer files for large inputs. |
| `loadWorld(file, maxShDegree, splatBudget, residencyBudget)` | Applies these load options on the render thread before this file's decode. |
| `loadTiledWorld(tileset)` | Streams a tiled world from its `tileset.json` within `residencyBudget`. |
| `cameraPose` | Read/set position in meters and yaw/pitch in radians. |
| `applyQuality(RenderQuality)` | Apply preset; individual properties can override it. |
| `renderScale` | Render-target scale, 0.1–2; changing it changes image quality. |
| `shDegree`, `maxShDegree` | Draw/load SH cap, 0–3. Set the load cap before loading. |
| `splatBudget` | LOD capacity for subsequent loads; zero disables automatic tree construction. Prebuilt LOD files still select a hierarchy. GPU maximum: 2.2M. |
| `residencyBudget` | Resident splats for subsequent streamed worlds, separate from LOD selection. |
| `cullMarginDegrees` | CPU fallback's angular margin; GPU visibility uses current-camera projected bounds. |
| `linearBlending` | Optional linear-light blend; not the default trained-space compositing. |
| `setMotionEnabled`, `setWalkVelocity`, `walk` | Gyroscope, continuous forward/right velocity in meters per second, and a single step in meters. |
| `look(deltaYaw, deltaPitch)` | Turns the camera by radians, for a look pad or mouse. |
| `touchLookEnabled`, `lookSensitivity` | Whether a one-finger drag turns the camera, and radians per pixel dragged. |
| `motionToggleEnabled` | Whether a double tap toggles the gyroscope. |
| `setCharacter(CharacterSettings)`, `character` | The walker's shape in walk mode: `eyeHeight`, `bodyRadius`, `stepHeight`. |
| `cameraPoseIntervalMillis`, `cameraPoseListener` | Milliseconds between pose callbacks, and the callback itself; 0, the default, never reports. |
| `isAvailable`, `gpuDescription` | Renderer availability and driver description. |
| `applyRenderPolicy(policy)`, `renderPolicy`, `deviceCapabilities` | Per-instance renderer policy, re-validated on the render thread. Only `sortDepth` and `subpixelThreshold` apply, with GPU visibility; other fields fall back with a warning each. |
| `readStats()` | FPS, frame/GPU/sort ms, loaded/drawn and screen-tile counts. Drawn counts and GPU ms describe the newest frame the GPU finished, also while nothing redraws, and are current when `onWorldFrameReady` fires. |
| `startBenchmark(seconds)` | Turn-in-place benchmark logged under `SplatKit`. |

Unavailable GPU timings/tile counters report zero, not zero-cost execution.
Loaded source splats differ from drawn nodes and GPU-resident hierarchy records.
Stats float transport exactly represents integers through 16,777,216.
Optional `com.splatkit.ui.SplatHudView` and `JoystickView` are host conveniences.

## GPU contract

GPU LOD → visibility/compaction → stable radix → indirect hardware draw.
Full32 keys are default; a `sortDepth` of 16 selects approximate two-pass sorting.
Above 3M visibility survivors the draw fails closed with diagnostics.
Source capacity depends on `maxStorageBufferRange` and memory; full LOD hierarchy residency is required.
Parents and subpixel rejection are approximate.
There is no universal 10M or 30/60 FPS guarantee.
Hybrid compute screen tiles remain Metal-only.

[Architecture, limits and evidence](docs/VULKAN.md) ·
[Agent harness](../../docs/AGENT_HARNESS.md) ·
[iOS SDK](https://github.com/Xget7/splatkit-ios)

```sh
cd apps/android-dev
./gradlew :splatkit:assembleRelease :splatkit:testDebugUnitTest
```

Tests and emulator evidence do not replace physical Adreno/Mali validation or reference-image acceptance.
Source builds enable 16 KB ELF alignment; CI checks every packaged shared library and APK ZIP alignment with `scripts/check_android_alignment.py`.
Artifact alignment checks do not replace execution on a 16 KB device.
