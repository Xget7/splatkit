---
name: ios-perf
description: Benchmark runs of the iOS Metal renderer on a physical iPhone, with optional Metal System Trace. Use to record a baseline, compare runs before and after a renderer change, or capture a trace to split GPU passes.
---

# iOS performance

A **run** is one release build of the dev app doing its benchmark turn on an iPhone: [scripts/ios_perf.py](../../../scripts/ios_perf.py) installs it, runs the turn through `devicectl` with the HUD off, and writes one run directory under `output/ios-perf/`.
Its `--help` carries the flags.

## Steps

1. **Check the phone.** `xcrun devicectl list devices` shows it `available (paired)`; take its UDID from there.
   The screen is unlocked and stays awake, and nothing else holds the GPU (Xcode GPU capture, Instruments).
   The script stops at once on a locked phone.
2. **Record.** `python3 scripts/ios_perf.py --udid <UDID> --seconds 30` with the same `--quality`, `--pose` and `--world` for every run you will compare.
   Pass `--skip-build` only when the last release build in `build/ios-dev-derived` is from the commit you mean to measure.
   Done when it prints `status passed`.
   For a comparison, record at least two runs per side; the run-to-run spread is the noise floor.
3. **Trace** when the numbers leave a pass's cost open: add `--trace` to one extra run.
   It records Metal System Trace for the whole turn into `metal.trace`, and the run fails when the trace is missing.
   Over Wi-Fi, saving the trace takes about 5 minutes and about 200 MB.
   Keep traced runs out of the comparison, since tracing adds load.
4. **Read** each run directory: `report.json` (the summary), `console.log` (every engine line), `metal.trace` (open in Instruments).
   Done when every figure in your report traces back to one of those files.
5. **Report** the device, iOS version, commit and dirty flag from `report.json` `run`, every skipped step, and each conclusion with the figures behind it.
   A difference counts only when it exceeds the spread between runs of the same side.

## Reading a run

The turn is one full rotation at a fixed position, the gyroscope off; `--pose` keeps the position and the turn resets yaw.
The frame budget is the display interval: 8.3 ms at 120 Hz, 16.7 ms at 60 Hz.

`report.json` `runs`, parsed from the engine's `SplatKit I:` lines:

- **fps, frame p95**: render-loop intervals over the turn, the headline numbers.
- **gpu ms** (`gpu_mean_ms`, p50 to p99): GPU time of the render command buffer, read each frame from the latest completed one, so values can lag or repeat.
- **sort, select ms** (`stage_sample_mean_ms`): periodic samples of the visibility plus sort buffer and of the LOD buffer.
- **drawn** (`drawn_sample_range`): the fewest and most splats drawn across the samples.

`report.json` `gpu` is the device's newest Apple GPU family, Metal 3 or 4, and its limits, from the renderer's `Metal capabilities:` startup line.

Each figure is `GPUEndTime - GPUStartTime` of one command buffer.
They are separate buffers on one queue, so `sort` can exceed `gpu`, and the frame's GPU cost is up to their sum, less any overlap the trace shows.
A pass inside one buffer has no time of its own; Metal System Trace splits it.

## Validation

`--validation` sets `MTL_DEBUG_LAYER=1` and `MTL_SHADER_VALIDATION=1`; `console.log` then starts with `Metal API Validation Enabled` and `Metal GPU Validation Enabled`.
Any validation message fails the run.
Its timings are not comparable with unvalidated runs.

## Limits

- `report.json` sees GPU time per command buffer, frame pacing and drawn counts; per-pass time, GPU counters, bandwidth and memory come from the trace.
- A world that is missing on the phone fails the run with `SplatWorldMissing`; the app never falls back to the default world.
- A touch moves the camera, so a run whose pitch changes during the turn fails: leave the phone untouched until the script ends.
