# splatkit-ios

Gaussian splat rendering for iOS: the shared SplatKit engine drawn with Metal, wrapped in a `UIView`.

## Use it

Build the static libraries with `scripts/build-ios.sh` from the repository root, then add to your target:

- the Swift sources in `Sources/SplatKit`,
- a bridging header importing `SplatKit/SKSplatEngine.h`, with `Sources/SplatKitCore/include` in the header search paths,
- `build/ios/lib` in the library search paths and `-lsplatkit_ios -lsplatkit_engine -lsplat_core -lspz -lzstd -lz -lc++` in the linker flags,
- the Metal, QuartzCore, CoreMotion, ImageIO, CoreGraphics and UniformTypeIdentifiers frameworks.

A Swift package with a binary xcframework is planned; `apps/ios-dev/project.yml` is the reference setup until then.

```swift
let view = SplatMetalView()
view.delegate = self
view.loadWorld(file: documents.appendingPathComponent("scene.spz"))
view.loadCollider(file: documents.appendingPathComponent("collider.glb"))
view.setMotionEnabled(true)
view.resume()
```

Forward `resume()`, `pause()` and `release()` from the host's lifecycle; the layer follows the view's window.

## API

`SplatMetalView` mirrors `SplatSurfaceView` on Android:

| Member | What it does |
| --- | --- |
| `loadWorld(file:)` | Decodes and shows a `.spz` or `.ply` world; the file is mapped, not copied |
| `loadTiledWorld(tileset:)` | Streams a tiled world made by `splat-tile` within `residencyBudget` |
| `loadCollider(file:)` | Decodes a GLB mesh and enables walk mode |
| `cameraPose` | Position, yaw and pitch; set it to teleport |
| `renderScale` | Fraction of the view's resolution the splats are drawn at, 0.1 to 2 |
| `cullMarginDegrees` | Angular margin kept drawn around the view |
| `linearBlending` | Blend in linear light instead of the encoded colour space |
| `splatBudget`, `residencyBudget` | Most splats drawn per frame, most splats resident on the GPU |
| `shDegree`, `maxShDegree` | Harmonics drawn, harmonics kept from the file |
| `setWalkVelocity(forward:right:)` | Continuous walking in meters per second |
| `setMotionEnabled(_:)`, `isMotionEnabled` | Gyroscope driven camera |
| `startBenchmark(seconds:)` | A reproducible turn with the frame time distribution logged |
| `captureFrame(to:completion:)` | The next frame as a PNG |
| `readStats()`, `gpuDescription` | Frame, GPU and sort times, splats drawn, device name |
| `delegate` | World and collider outcomes, on the main thread |

Gestures: one finger looks, two fingers walk, a double tap toggles the gyroscope; `lookSensitivity` and `walkSensitivity` scale them.

## Layout

```
Sources/SplatKitCore/rendering/    MetalSplatRenderer (Objective-C++) and Splat.metal, the SplatRenderer implementation
Sources/SplatKitCore/engine/       SKSplatEngine, the Objective-C boundary over the shared engine
Sources/SplatKitCore/include/      the public header Swift imports
Sources/SplatKit/                  RenderThread, MotionInput, SplatMetalView
cmake/                             embeds the shader source into the library
```

The shader is compiled at run time from the embedded source, so the library is a plain static archive with no metallib to ship.
The renderer keeps two frames in flight and reads GPU time from the command buffer.
The visible order is made on the GPU (`MetalVisibility`, ADR 0017): a cull of the slab ranges to draw, a radix sort by distance and an indirect draw, so the CPU never sorts for this renderer; the sort's own time is what `readStats().sortMillis` reports.
The sort has unit tests that run on a Mac: `cmake -S packages/splatkit-ios -B build/ios-mac && cmake --build build/ios-mac && ctest --test-dir build/ios-mac`.
Colours blend in the encoded space by default, on a `bgra8Unorm` layer; `linearBlending` switches the layer to `bgra8Unorm_srgb`.
