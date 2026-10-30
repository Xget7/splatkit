# Metal reference facts

Cited facts for the [prior-results review](../README.md#before-implementation), covering the Metal renderer's capabilities and limits.
Each fact names its source; [scripts/fetch-gpu-refs.sh](../../../scripts/fetch-gpu-refs.sh) puts the full Apple texts in `build/gpu-refs/metal/` for grep, since Apple's license allows personal copies only.
Refresh a fact in the same pull request as the GPU pipeline change that needs it.

## Sources

| Key | Source | Version |
|---|---|---|
| FST | [Metal Feature Set Tables](https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf) | 2026-05-21, fetched 2026-10-20 |
| MSL | [Metal Shading Language Specification](https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf) | 4.1, 2026-06-04, fetched 2026-10-20 |
| SDK | `Metal.framework/Headers` in `xcrun --sdk iphoneos --show-sdk-path` | iPhoneOS 27.1 SDK, Xcode 27.1 |
| DOC | Xcode MCP `DocumentationSearch`, or `https://developer.apple.com/documentation/metal/<symbol>` | live |

Precedence: SDK, then MSL and FST, then DOC.
The headers are what compiles.

## Our floor

- GPU: `MTLGPUFamilyApple7` (A14, M1), required in [MetalContext.mm](../../../packages/splatkit-ios/Sources/SplatKitCore/rendering/metal/device/MetalContext.mm).
  FST: every Metal 3 and Metal 4 device meets at least the Apple7 limits, and the Metal 4 programming model starts at Apple7.
- OS: iOS 17 ([Package.swift](../../../Package.swift)).
- Shading language: `MTLLanguageVersion2_3` ([MetalShaderLibrary.h](../../../packages/splatkit-ios/Sources/SplatKitCore/rendering/metal/device/MetalShaderLibrary.h)).
- Kernels assume `threadExecutionWidth == 32` and 256-thread threadgroups, checked at pipeline creation.
- Reference phone: iPhone 17 Pro, A19 Pro, family Apple10 (FST GPU table).
  It reports family Apple10, Metal 4, threadgroup memory 32768 B, threads per threadgroup 1024, max buffer 6442450944 B and working set 8589950976 B on iOS 27.0 (ios-perf `gpu`, 2026-10-20).
- `MTLGPUFamilyApple11` is in the iPhoneOS 27.1 SDK `MTLDevice.h`; the FST has no Apple11 column, as of 2026-10-20.

## Features we use

All available from Apple7 on the Metal 3 model (FST feature availability).

| Feature | From | Where |
|---|---|---|
| SIMD-scoped reduction (`simd_sum`, `simd_prefix_exclusive_sum`) | Apple7 | sort, visibility, LOD |
| SIMD-scoped permute (`simd_shuffle`, `simd_broadcast`, `simd_ballot`) | Apple6 | sort, visibility |
| Function specialization | Apple2 | SH degree, LOD and sort variants |
| Indirect draw and dispatch arguments | Apple3 | 7 indirect draws, sort and LOD dispatches |
| Relaxed `atomic_uint` (device and threadgroup) | all | compaction, radix bins |

## Shader compilation

- Metal builds and caches shaders on the device running the app, so a pipeline state the app created before compiles faster on the next launch; `MTLBinaryArchive` removes only the first launch's compile (DOC, Shader libraries, fetched 2026-10-22).
- Create `MTLComputePipelineState` objects at a noncritical time, since creation can compile (DOC, `MTLComputePipelineState`, fetched 2026-10-22).

## Limits that bind us

From FST implementation limits, Apple7 column (equal or higher on Apple8 to Apple10).

| Limit | Apple7 | Our use |
|---|---|---|
| Total threadgroup memory per kernel | 32 KB | radix scatter about 10.3 KB |
| Threadgroup memory length alignment | 16 B | |
| Threads per threadgroup (theoretical; read `maxTotalThreadsPerThreadgroup`) | 1024 | 256 |
| Buffer argument table entries per function | 31 | |
| Inline bytes per `setBytes` | 4 KB | |
| Function constants | 65,536 | 4 |
| Residency sets per queue | 32 | none yet |
| Counter sample buffer length | 32 KB | none yet |
| Implicit imageblock allocation | 128 KB (256 KB from Apple9) | none |

## OS gates for newer API

The FST lists GPU families only; the OS that ships an API comes from `API_AVAILABLE` in the SDK headers.

| API | iOS | Source |
|---|---|---|
| `MTLLanguageVersion3_1` | 17 | SDK `MTLLibrary.h` |
| `MTLLanguageVersion3_2`, `thread_scope` on fences | 18 | SDK `MTLLibrary.h`, MSL 6.16.2 |
| `MTLResidencySet` (FST: Apple6) | 18 | SDK `MTLResidencySet.h` |
| Per-pipeline `shaderValidation` | 18 | SDK `MTLPipeline.h`, `MTLComputePipeline.h` |
| `MTLLanguageVersion4_0`, `MTL4*` command model, argument tables, command allocators, barriers (FST: Apple7) | 26 | SDK `MTLLibrary.h`, `MTL4CommandQueue.h` |
| `requiredThreadsPerThreadgroup` on compute pipelines | 26 | SDK `MTLComputePipeline.h` |
| `MTLLanguageVersion4_1` | 27 | SDK `MTLLibrary.h` |
| `memory_order_acquire`, `_release`, `_acq_rel` on atomics and barriers (FST: Apple7) | 27 (MSL 4.1) | MSL 1.3, 6.16.1 |
| Clamp-to-edge, integer-offset and 2x1 / 1x2 block texture reads (FST: Apple7) | 27 (MSL 4.1) | MSL 1.3, 6.13 |
| `forwardProgressUsage` (`automatic`, `weak`, `simdGroupParallel`), `contentionRelief`, `optimizeForPersistentKernel`, `recommendedPersistentThreadgroupsPerGridForThreadsPerThreadgroup:` | 27 | SDK `MTLPipeline.h`, `MTLComputePipeline.h`; DOC pages exist with no text and MSL 4.1 does not mention them, as of 2026-10-20 |

Anything above iOS 17 needs an availability check and a fallback, or a raised floor.

## Validation

- API Validation: launch with `MTL_DEBUG_LAYER=1` ([Validating your app's Metal API usage](https://developer.apple.com/documentation/xcode/validating-your-apps-metal-api-usage)).
  It checks unretained resource references only on `MTLCommandBuffer`, never on `MTL4CommandBuffer`.
- Shader Validation: launch with `MTL_SHADER_VALIDATION=1`, scoped with `MTL_SHADER_VALIDATION_ENABLE_PIPELINES` / `_DISABLE_PIPELINES` by pipeline label ([Validating your app's Metal shader usage](https://developer.apple.com/documentation/xcode/validating-your-apps-metal-shader-usage)).
  It reports out-of-bounds and non-resident reads, threadgroup memory and NaN/Inf faults.
- Neither reports a missing barrier or a data race; prove synchronization with a numeric test that compares GPU output against a CPU reference.

## Candidates worth a measured trial

Each needs the GPU change workflow and a phone run before it lands.

- Single-pass radix sort with decoupled look-back: needs acquire/release atomics (iOS 27) and a forward-progress guarantee between threadgroups; the iOS 27 forward-progress and persistent-kernel pipeline options look aimed at this but are undocumented, so their guarantee must be proven on the phone first.
- SIMD shift and fill (FST: Apple8) for scans: above our Apple7 floor, so it needs a family check.
- 64-bit atomics (FST: Apple9; Apple8 macOS min/max only) for packed key-plus-index sorting: above the floor.
- Residency sets (iOS 18) and the `MTL4` command model (iOS 26): planned in the Metal 4 backend spec.
