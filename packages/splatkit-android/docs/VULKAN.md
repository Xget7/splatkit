# Vulkan

Public API: `SplatSurfaceView`; JNI and Vulkan classes are internal.
Current source enables `VulkanFrameCompute`: GPU LOD → visibility → stable radix → indirect draw.
Alpha05 integrates this path; Maven alpha04 predates it. Hybrid tiles and RN GPU options remain pending.

Contracts live beside code: [VisibilityPass.h](../src/main/cpp/rendering/vulkan/VisibilityPass.h), [shader ABI](../src/main/cpp/rendering/vulkan/VulkanShaderTypes.h).
The context outlives allocations; reuse slots after fences, resize/destroy after all consumers finish.
The caller owns input sizes, alignment, upload dependencies and readback invalidation.
Unsorted output requires sorting before drawing.
Subgroup width is device-dependent; subpixel rejection is approximate.
Visibility scratch: `16*capacity+64` bytes across two slots; radix adds ~99MB at 3M.
Source capacity obeys `maxStorageBufferRange/32`; 10M is not guaranteed.
Survivor overflow above 3M zeros the draw with diagnostics; it never silently truncates.
Full32 sorting is default; internal `SPLATKIT_VULKAN_SORT_BITS=16` uses two passes on
quantized keys stored in `uint32`, without requiring native 16-bit arithmetic/storage.

Evidence (2026-09-12): Android arm64 emulator / M4 Pro via MoltenVK passed
128 radix cases through 3M, visibility, LOD-to-indirect integration and shader-free upload pressure;
native suites had layers off. Kitchen 500k loaded/drew with layers on and no captured validation errors.
SwiftShader passes smaller suites but crashes on large mapped uploads, also reproduced without SDK code.
No physical Android performance or reference-image quality acceptance follows.

## Metal mapping

| Metal | Vulkan / GLSL |
|---|---|
| `threadgroup` memory | `shared` / SPIR-V Workgroup; not framebuffer tile memory (GMEM) |
| `simd_prefix_exclusive_sum`, `simd_sum` | `subgroupExclusiveAdd`, `subgroupAdd`; query subgroup capabilities/width |
| `threadgroup_position_in_grid` | `gl_WorkGroupID`; `local_size` specifies group dimensions instead |
| `device T*` buffer | Storage-buffer descriptor + `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT` |

References: [subgroups](https://docs.vulkan.org/guide/latest/subgroups.html),
[built-ins](https://docs.vulkan.org/glsl/latest/chapters/builtins.html),
[16-bit arithmetic/storage](https://docs.vulkan.org/samples/latest/samples/performance/16bit_arithmetic/README.html).
