# Android Fabric adapter

`SplatKitPackage` registers `SplatKitView` through the generated Codegen delegate.
The module needs compileSdk 36 or the app's `rootProject.ext` value, minSdk 29, Java 17 and arm64-v8a.
It applies `com.facebook.react`, so the consuming app autolinks it and runs its Codegen.
It depends on `io.github.xget7:splatkit-android:0.1.0-alpha07`, or on `project(':splatkit')` when the build defines one, as in the SplatKit repository.

Host tests, from the SplatKit repository root after `npm ci` in the package:

```sh
apps/react-native-dev/android/gradlew -p packages/react-native-splatkit/android/verification :adapter:compileDebugKotlin :adapter:testDebugUnitTest
```

The verification build has no `:app` to seed plugin Codegen, so it runs `scripts/codegen-android.cjs` instead; see `android/verification/build.gradle`.
The host tests cover request validation, stale outcomes, upload/frame ordering, the stats rate limit and policy prop parsing.
They do not mount Fabric, exercise Activity/surface lifecycle or run Vulkan.

Each accepted replacement gets a separate native view/engine because SDK callbacks have no request ID.
Old callbacks are detached and generation-filtered; release waits asynchronously for outstanding decode work, so replacement does not cancel decoding.
The previous world is removed on an accepted replacement; failed loading can leave the view blank.
Load options are applied before that engine's sole decode is scheduled.
Null `world` releases the current engine; host pause, view detachment and `paused` stop rendering, and manager drop/host destruction release resources.
The view starts its load when attached.
Fabric does not lay out native children added after mount, so the adapter measures each new engine's surface view to its own bounds.

`frameReady` uses GPU fence completion, not upload or display scanout.
Count snapshots are emitted at most twice a second after frame readiness, the first describing that frame or a newer one; the SDK's float transport can round counts above 16,777,216.
Timing availability is false because the public SDK cannot distinguish unavailable timestamps from real zero-duration measurements.
Props are committed once per Fabric transaction, so a world change with display settings or a policy configures only the new engine, once.
Each engine emits `onCapabilities`, then applies the current policy before its decode is scheduled; a later revision is applied to the live engine.
Policy events carry the revision and policy captured at request time and are dropped when the engine was replaced.
Only local world files are accepted; tileset loading is not wired into this adapter.
