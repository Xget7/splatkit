---
name: gpu-renderer-change
description: Plan, implement, refactor and measure a change to the GPU pipeline on both the Metal and the Vulkan renderer, grounding every API and limit in current sources. Use when changing, optimizing or porting shaders, passes, sort, culling, LOD selection, GPU resource layout or synchronization on either backend.
---

# GPU renderer change

Physical acceptance and benchmark evidence use the [12M reference world](../../../docs/VALIDATION.md#reference-performance-world); small fixtures establish only smoke or unit-test results.

A **run** takes one change to the GPU pipeline from a measurable hypothesis to a merged pull request with phone numbers behind it, on **both backends**.
Metal and Vulkan carry equal weight: a change that lands on one backend only is unfinished, and a gap between them is the first thing a run closes.
Every API, flag and limit the run touches is **grounded**: it carries a citation to a current source, because model memory lags the SDKs by a release or more.

| | Metal | Vulkan |
|---|---|---|
| Renderer | `packages/splatkit-ios/Sources/SplatKitCore/rendering/metal/` | `packages/splatkit-android/src/main/cpp/rendering/vulkan/` |
| Cited facts and floor | [docs/gpu-refs/metal/README.md](../../../docs/gpu-refs/metal/README.md) | [docs/gpu-refs/vulkan/README.md](../../../docs/gpu-refs/vulkan/README.md) |
| Header that compiles | `Metal.framework/Headers` in `xcrun --sdk iphoneos --show-sdk-path` | `vulkan/vulkan_core.h` in the NDK sysroot |
| Docs server | Xcode MCP `DocumentationSearch` | Vulkan docs MCP `vulkan-site`: `search-vulkan-docs` |
| Semantics | `developer.apple.com/documentation/metal/<symbol>` | the Vulkan specification at `docs.vulkan.org/spec/latest/` |
| GPU tests | `packages/splatkit-ios/tests/rendering/metal/` | `packages/splatkit-android/tests/rendering/vulkan/` |
| Phone run | [ios-perf](../ios-perf/SKILL.md) | [android-perf](../android-perf/SKILL.md) |

## Steps

Every run opens with a **docs check**, before reading code or gathering anything about the problem: one search for the change's subject on each docs server in the table, both backends.
A server that errors or is missing is a note, never a stop: tell the user, write it under Grounding in the design doc, and cite that backend from its header and specification instead.
Done when each server has answered or its failure is noted.

1. **Gate.** The run applies when the change alters what the GPU executes or how its data flows: shaders, passes, dispatch shape, sort, culling, LOD selection, buffer layout, storage modes, synchronization, frame slots.
   Anything else (host logic, API surface, docs, a fix confined to CPU code) goes through the ordinary pull request flow and ends here.
   Done when you can name the GPU stage the change alters.

2. **Compare the backends** at that stage before planning anything new: read both implementations and list where they differ.
   A difference that is a port gap becomes part of this run, or the run itself when the change is a port.
   A difference kept on purpose (an API with no counterpart on the reference phone) is written into the design doc with its reason.
   Done when every difference at the stage is a port gap in the plan or a reasoned exception.

3. **Ground.** List every API, shading-language feature and limit the change will use or rely on, per backend, then cite each one from the table above.
   The header wins on availability, since it is what compiles; the refs README holds the spec texts and the source precedence.
   Check each against that backend's floor, and each limit against the reference phone's capabilities in a phone run report; anything above the floor needs a runtime check plus a fallback, or a raised floor stated in the hypothesis.
   An API with headers and no documented semantics is a probe target, never a plan dependency.
   Done when every item on both backends has a citation and a floor verdict.

4. **Plan.** Write the design doc at `docs/research/plans/<date>-<slug>.md` from [design-doc-template.md](design-doc-template.md).
   The hypothesis names a metric, a scene, a pose and the number it expects to move on each reference phone, for example "sort drops from 6.1 to 4 ms on lioness at the orbit start on both phones".
   Done when every template section is filled for both backends, every API in it is grounded, and the user has approved the doc.

5. **Baseline.** Record at least two phone runs per backend at the base commit, with the scene and pose from the hypothesis.
   Done when the runs exist and each backend's spread is known: the spread is the noise floor for step 8.

6. **Implement** against the design doc on a branch, in small tasks; subagents may take tasks whose files the doc names.
   Each task starts from a failing GPU test on its backend: a numeric comparison of GPU output against a CPU reference, since no validation layer reports a missing barrier or a race.
   A behaviour both backends share gets the same test case on both.
   Metal tests run on the Mac with `MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1` in the environment.
   Vulkan tests are device binaries (CMake target `splatkit_android_tests`); `python3 scripts/android_device_tests.py` builds them and runs each on the reference phone over adb.
   A user-build phone loads no validation layer for shell binaries, so Vulkan validation is a phone run of the debug dev app: `python3 scripts/android_perf.py --validation`, with the layer from [scripts/fetch-validation-layers.sh](../../../scripts/fetch-validation-layers.sh).
   Done when `python3 scripts/sdk_harness.py check metal` and `check android` pass, `android_device_tests.py` passes, and both validation runs (`ios_perf.py --validation`, `android_perf.py --validation`) pass.

7. **Refactor** on a clean branch with an allow-list of files from the design doc.
   Read the whole diff for broad barriers or encoder splits where a narrower one holds, host-visible memory the CPU never touches, per-frame allocation, leftover debug paths, and the two backends drifting apart in naming, constants or structure.
   Done when the diff touches only allow-listed files and step 6's checks still pass.

8. **Measure.** Record at least two phone runs per backend at the change, same scene, pose and quality as step 5.
   A difference counts only when it exceeds that backend's baseline spread.
   Hypothesis missed on either backend: write the measured numbers into the design doc and return to step 4.
   Done when the hypothesis is confirmed or refuted on both backends with figures from the run files.

9. **Close.** Add any newly cited fact to the backend's refs README with its source and fetch date.
   Ship through the ship skill; the PR's Evidence carries the hypothesis, both backends' figures with their spread, and the device, OS and commit of every run.
   Done when the pull request is open with that Evidence for both backends.

## Reading the numbers

[ios-perf](../ios-perf/SKILL.md) and [android-perf](../android-perf/SKILL.md) say what each figure times and when a pass needs a trace.
