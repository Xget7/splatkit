---
name: metal-renderer-change
description: Plan, implement, refactor and measure a change to the Metal renderer's GPU pipeline, grounding every API and limit in current Apple sources. Use when changing or optimizing Metal shaders, passes, sort, culling, LOD selection, GPU resource layout or synchronization.
---

# Metal renderer change

A **run** takes one change to the GPU pipeline from a measurable hypothesis to a merged pull request with phone numbers behind it.
Every API, flag and limit the run touches is **grounded**: it carries a citation to a current source, because model memory lags the SDK by a release or more.
Facts we already cite live in [docs/gpu-refs/metal/README.md](../../../docs/gpu-refs/metal/README.md), with the source precedence and our floor (GPU family, OS, shading language).

## Steps

1. **Gate.** The run applies when the change alters what the GPU executes or how its data flows: shaders, passes, dispatch shape, sort, culling, LOD selection, buffer layout, storage modes, synchronization, frame slots.
   Anything else (host logic, API surface, docs, a fix confined to CPU code) goes through the ordinary pull request flow and ends here.
   Done when you can name the GPU stage the change alters.

2. **Ground.** List every API, MSL feature and limit the change will use or rely on, then cite each one:
   - the SDK header line (`xcrun --sdk iphoneos --show-sdk-path`, then `System/Library/Frameworks/Metal.framework/Headers`), which wins on availability since it is what compiles;
   - the Xcode MCP `DocumentationSearch` result, or `https://developer.apple.com/documentation/metal/<symbol>`, for semantics;
   - the MSL specification or Feature Set Tables text in `build/gpu-refs/metal/` (run [scripts/fetch-gpu-refs.sh](../../../scripts/fetch-gpu-refs.sh) when it is missing) for language rules, GPU family and limits.
   Check each against our floor; anything above it needs a runtime check plus a fallback, or a raised floor stated in the hypothesis.
   An API with headers and no documented semantics is a probe target, never a plan dependency.
   Done when every item has a citation and a floor verdict.

3. **Plan.** Write the design doc at `docs/research/plans/<date>-<slug>.md` from [design-doc-template.md](design-doc-template.md).
   The hypothesis names a metric, a scene, a pose and the number it expects to move, for example "sort drops from 6.1 to 4 ms on lioness at the orbit start".
   Done when every template section is filled, every API in it is grounded, and the user has approved the doc.

4. **Baseline.** Record at least two [ios-perf](../ios-perf/SKILL.md) runs on the reference phone at the base commit, with the scene and pose from the hypothesis.
   Done when the runs exist and their spread is known: the spread is the noise floor for step 7.

5. **Implement** against the design doc on a branch, in small tasks; subagents may take tasks whose files the doc names.
   Each task starts from a failing Mac GPU test: a numeric comparison of GPU output against a CPU reference under `packages/splatkit-ios/tests/rendering/metal/`, since no validation layer reports a missing barrier or a race.
   Run the tests with `MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1` in the environment.
   Done when `python3 scripts/sdk_harness.py check metal` passes and the validation-enabled run prints no Metal validation message.

6. **Refactor** on a clean branch with an allow-list of files from the design doc.
   Read the whole diff for broad barriers or encoder splits where a narrower one holds, Shared storage on buffers the CPU never touches, per-frame allocation, and leftover debug paths.
   Done when the diff touches only allow-listed files and step 5's checks still pass.

7. **Measure.** Record at least two ios-perf runs at the change, same scene, pose and quality as step 4.
   A difference counts only when it exceeds the baseline spread.
   Hypothesis missed: write the measured numbers into the design doc and return to step 3.
   Done when the hypothesis is confirmed or refuted with figures from the run files.

8. **Close.** Add any newly cited fact to `docs/gpu-refs/metal/README.md` with its source and fetch date.
   Ship through the ship skill; the PR's Evidence carries the hypothesis, both sides' figures with their spread, and the device, OS and commit of every run.
   Done when the pull request is open with that Evidence.

## Reading the numbers

[ios-perf](../ios-perf/SKILL.md) says which command buffer each figure times and when a pass needs Metal System Trace.
