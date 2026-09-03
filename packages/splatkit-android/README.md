# splatkit-android

Android engine: Vulkan renderer, camera, input, scene and view.
Consumes `splat-core` for formats and navigation.
No React Native dependency; see `apps/android-dev` for a plain Android host.

## Domains

| Domain | Language | Responsibility |
|---|---|---|
| Rendering | C++ (`src/main/cpp/rendering`) | Vulkan context, swapchain, frame loop, splat pipeline, sorter |
| View | Kotlin (`SplatSurfaceView`, `RenderThread`) | Surface lifecycle, Choreographer-driven render thread, JNI boundary |
| Camera, Input, Scene | pending | |
