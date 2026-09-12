# 0021 - Mobile backend parity

Decision: equivalent host capabilities through [SplatRenderer](../../packages/splatkit-engine/include/splatkit/rendering/SplatRenderer.h), with backend-specific GPU implementations.

Implemented: [Metal](../../packages/splatkit-ios/Sources/SplatKitCore/rendering/MetalSplatRenderer.mm) wires visibility, radix, LOD and optional hybrid tiles; [Vulkan](../../packages/splatkit-android/src/main/cpp/rendering/vulkan/VulkanSplatRenderer.cpp) uses CPU ordering, with [visibility](../../packages/splatkit-android/src/main/cpp/rendering/vulkan/VisibilityPass.h) inactive.
Vulkan GPU LOD and screen tiles are absent.

Next: activate visibility → 32-bit radix → indirect draw, preserving blend-compatible order and [compute/graphics dependencies](https://github.khronos.org/Vulkan-Site/guide/latest/synchronization_examples.html); retain CPU fallback.
Gate [Vulkan subgroups](https://docs.vulkan.org/guide/latest/subgroups.html) and [Metal features](https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf), not OS versions alone.

Reuse [LOD data](../../packages/splat-core/include/splat/lod/LodTree.h), original leaves and covering-cut policy; moment-matched parents lack [hierarchy optimization](https://repo-sam.inria.fr/fungraph/hierarchical-3d-gaussians/), temporal transitions and image-error guarantees.
[Screen tiles](../../packages/splatkit-ios/Sources/SplatKitCore/rendering/MetalTileRaster.h) require complete ordered candidates or hardware fallback.
Inference from [3DGS compositing](https://raw.githubusercontent.com/graphdeco-inria/diff-gaussian-rasterization/main/cuda_rasterizer/forward.cu): transparent splats cannot serve as opaque Hi-Z occluders; transmittance termination is approximate.
[Mobile-GS](https://xiaobiaodu.github.io/mobile-gs-project/) changes representation and compositing; it is not a drop-in lossless optimization.

Alpha acceptance requires [host checks](../AGENT_HARNESS.md), validation-clean rendering, image comparisons, lifecycle tests and sustained physical-device timings with world/settings/driver provenance.
No lossless, universal 30/60 FPS or state-of-art claim follows.
This review ran no builds, emulator checks or physical-device tests.
