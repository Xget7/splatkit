# splatkit-android

Native `SplatSurfaceView`, Vulkan renderer, walk/fly camera, touch and gyroscope input.
Requires Android 10/API 29, Vulkan 1.1 and arm64-v8a.
GPU ordering additionally requires supported compute subgroups and buffer limits.
An arm64 emulator can test functionality; it is not a phone performance measurement.

## Install

New GPU integration: `0.1.0-alpha05`. Check [releases](https://github.com/Xget7/splatkit-android/releases) for publication status.
Maven `0.1.0-alpha04` is the older CPU-ordering artifact.

```kotlin
dependencies {
    implementation("io.github.xget7:splatkit-android:0.1.0-alpha05")
}
```

For current source, use `implementation(project(":splatkit"))` in the included
`apps/android-dev` host, or include `packages/splatkit-android` as a Gradle module.

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
Use `loadCollider(File)` for optional GLB walk collision; otherwise the camera flies.
SPZ v2–v4 and offline `.lodsplat` worlds are supported. PLY needs offline conversion.
Compose can host the view through `AndroidView`; forward the same lifecycle.
React Native GPU controls are not implemented yet.

## Controls

| API | Contract |
|---|---|
| `loadWorld(bytes/file)`, `loadCollider(bytes/file)` | Load asynchronously; prefer files for large inputs. |
| `cameraPose` | Read/set position in meters and yaw/pitch in radians. |
| `applyQuality(RenderQuality)` | Apply preset; individual properties can override it. |
| `renderScale` | Render-target scale, 0.1–2; changing it changes image quality. |
| `shDegree`, `maxShDegree` | Draw/load SH cap, 0–3. Set the load cap before loading. |
| `splatBudget` | LOD capacity for subsequent loads; zero disables automatic tree construction. Prebuilt LOD files still select a hierarchy. GPU maximum: 2.2M. |
| `residencyBudget` | Resident splats for subsequent streamed worlds, separate from LOD selection. |
| `cullMarginDegrees` | CPU fallback's angular margin; GPU visibility uses current-camera projected bounds. |
| `linearBlending` | Optional linear-light blend; not the default trained-space compositing. |
| `setMotionEnabled`, `setWalkVelocity` | Gyroscope and continuous forward/right velocity. |
| `lookSensitivity`, `walkSensitivity` | Gesture tuning. |
| `isAvailable`, `gpuDescription` | Renderer availability and driver description. |
| `readStats()` | FPS, frame/GPU/sort ms, loaded/drawn and screen-tile counts. Completed snapshots can lag. |
| `startBenchmark(seconds)` | Turn-in-place benchmark logged under `SplatKit`. |

Unavailable GPU timings/tile counters report zero, not zero-cost execution.
Loaded source splats differ from drawn nodes and GPU-resident hierarchy records.
Stats float transport exactly represents integers through 16,777,216.
Optional `com.splatkit.ui.SplatHudView` and `JoystickView` are host conveniences.

## GPU contract

GPU LOD → visibility/compaction → stable radix → indirect hardware draw.
Full32 keys are default; internal `SPLATKIT_VULKAN_SORT_BITS=16` selects approximate two-pass sorting.
Above 3M visibility survivors the draw fails closed with diagnostics.
Source capacity depends on `maxStorageBufferRange` and memory; full LOD hierarchy residency is required.
Parents and subpixel rejection are approximate. There is no universal 10M or 30/60 FPS guarantee.
Hybrid compute screen tiles remain Metal-only.

[Architecture, limits and evidence](docs/VULKAN.md) ·
[Agent harness](../../docs/AGENT_HARNESS.md) ·
[iOS SDK](https://github.com/Xget7/splatkit-ios)

```sh
cd apps/android-dev
./gradlew :splatkit:assembleRelease :splatkit:testDebugUnitTest
```

Tests and emulator evidence do not replace physical Adreno/Mali validation or reference-image acceptance.
