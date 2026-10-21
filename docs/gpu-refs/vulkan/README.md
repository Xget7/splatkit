# Vulkan GPU references

Cited facts the [gpu-renderer-change](../../../.claude/skills/gpu-renderer-change/SKILL.md) skill grounds Vulkan plans in.
Each fact names its source; refresh it in the pull request that relies on it.

## Sources

Precedence on availability: the header, then the specification, then the reference phone's reported capabilities.

| Key | Source | Version |
|---|---|---|
| HDR | `vulkan/vulkan_core.h` in the NDK sysroot (`$ANDROID_HOME/ndk/<version>/toolchains/llvm/prebuilt/<host>/sysroot/usr/include`) | `VK_HEADER_VERSION` 275, NDK 27.1.12297006 |
| SPEC | Vulkan specification, `https://docs.vulkan.org/spec/latest/` | latest, reachable 2026-10-20 |

## Our floor

- API: Vulkan 1.1 with `VK_KHR_swapchain` ([VulkanContext.cpp](../../../packages/splatkit-android/src/main/cpp/rendering/vulkan/device/VulkanContext.cpp) `require_api_version(1, 1, 0)`).
- OS: Android 10, API 29 (`minSdk` in [build.gradle.kts](../../../packages/splatkit-android/build.gradle.kts)).
- GPU pipeline: a compute queue and subgroup basic, arithmetic and ballot operations in compute shaders, plus workgroup and storage-binding limits; a device without them sorts on the CPU ([VulkanVisibilityPass.cpp](../../../packages/splatkit-android/src/main/cpp/rendering/vulkan/cull/VulkanVisibilityPass.cpp) `queryCapabilities`, `VulkanSplatRenderer::sortsOnGpu`).
- Reference phone: Xiaomi Mi 9, Adreno 640, Android 11, Vulkan 1.1.128, driver `0x801f6000` ([BENCHMARKS.md](../../BENCHMARKS.md)).
  Its full limits are not captured yet.

## Adreno 640 findings

- Subgroup ballot loses keys in the radix histogram, so the histogram uses workgroup atomics ([ARCHITECTURE.md](../../ARCHITECTURE.md), Sort row).
- An sRGB swapchain attachment cost about 40 % of the frame, so the swapchain is UNORM unless linear blending is on ([ARCHITECTURE.md](../../ARCHITECTURE.md), Present row).
- With an identity pre-transform on the rotated display, SurfaceFlinger composited the app layer on the GPU (`dumpsys SurfaceFlinger`: CLIENT, ROT_90); pre-rotated, it is DEVICE with no transform (2026-10-20).
  On kitchen 500k in landscape that moved 37.6 and 37.7 fps to 37.9 and 38.0, GPU 26.2 and 26.3 ms to 26.1 and 26.0 ms.
  Method: [Android pre-rotation guide](https://developer.android.com/games/optimize/vulkan-prerotation); `currentExtent` comes in the window's orientation, the swapchain images take the swapped size.
