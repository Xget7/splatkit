---
name: android-perf
description: Perfetto performance runs of the Android Vulkan renderer on a physical phone. Use to record a baseline, analyze a trace, or compare runs before and after a renderer change.
---

# Android performance with Perfetto

A **run** is one release build of the dev app doing its benchmark turn under Perfetto: [scripts/android_perf.py](../../../scripts/android_perf.py) records it and [scripts/perfetto_analysis.py](../../../scripts/perfetto_analysis.py) reads the trace.
Both scripts' `--help` carry the flags.
Use the [12M reference world](../../../docs/VALIDATION.md#reference-performance-world) for physical acceptance and benchmarks; pass its installed filename explicitly and never substitute kitchen for representative evidence.
Before reporting or opening a PR, follow [visual benchmark evidence](../../../docs/VALIDATION.md#pr-benchmark-evidence): material effects require native Markdown before/after tables for the reviewing engineer, backed by the run files.
Every run locks the display to landscape (rotation 1) and restores the rotation settings afterwards, so runs compare whatever way the phone lies.

Use the shared [GPU profiling protocol](../../../docs/GPU_PROFILING.md) and `scripts/gpu_benchmark.py` preset for repeated comparisons; keep timing, timeline, frame capture and power experiments distinct.
Do not claim GPU passes were profiled from app-published FPS/stage counters alone.

## Steps

1. **Set up once.** Create the analysis venv if `build/perf-venv` is missing:
   `python3 -m venv build/perf-venv && build/perf-venv/bin/pip install -r scripts/requirements-perf.txt`.
   Done when `build/perf-venv/bin/python -c "import perfetto"` exits 0.
2. **Check the phone.** `adb devices -l` lists exactly one device (else pass `--serial`), the screen is unlocked and awake, and nothing else holds the GPU (Snapdragon Profiler capture, other apps).
3. **Record.** `python3 scripts/android_perf.py --seconds 30` with the same `--quality`, `--pose` and `--world` for every run you will compare.
   `--world` names a file pushed under `/sdcard/Android/data/com.splatkit.devapp/files/`; without it the run loads the bundled kitchen.
   An untagged World Labs `.spz` pushed as `--world` also needs `--source-frame rdf`; the bundled kitchen and `.lodsplat` worlds need none.
   Done when it prints `status passed`, `app slices in trace: True` and an analysis block.
   For a comparison, record at least two runs per side; the run-to-run spread is the noise floor.
4. **Read** each run directory: `report.json` (app timings from GPU timestamp queries), `run.json` (the [run file](../../../scripts/benchmarks/run.schema.json) the app was launched with), `analysis.json` (the trace), `trace.perfetto-trace` (open at https://ui.perfetto.dev for anything the numbers leave open).
   Done when every figure in your report traces back to one of those files.
5. **Report** the device, Android version, commit and dirty flag from `report.json` `run`, every skipped step, and each conclusion with the figures behind it.
   A difference counts only when it exceeds the spread between runs of the same side.

## Reading a run

The benchmark window is the `SplatKit benchmark` async slice: one full turn at a fixed pose with the gyroscope off and vsync requested off.
The frame budget is the display interval: 16.7 ms at 60 Hz.

`report.json`, from the app's GPU timestamp queries:

- **fps, frame p95** (`runs`): host-loop intervals over the turn, the headline numbers.
- **gpu ms** (`gpu_mean_ms`): GPU time per frame.
  Above the budget means GPU-bound.
- **sort, cull ms** (`stage_sample_mean_ms`): periodic GPU samples of the radix sort and visibility passes.
- **`gpu`**: the device, API, driver and the limits the pipeline relies on, from the `Vulkan device:` and `Vulkan capabilities:` startup lines ([VulkanContext.cpp](../../../packages/splatkit-android/src/main/cpp/rendering/vulkan/device/VulkanContext.cpp)).

`analysis.json`, from the trace:

- **`SplatKit submit and present`** (`render_thread_slices`): CPU blocked in `vkQueueSubmit` and `vkQueuePresentKHR`.
  Long, with the render thread mostly `S`, means it waits on the GPU or the display.
- **`SplatKit wait for frame slot`**: the fence wait for a free frame in flight.
  Long means the GPU is behind the CPU.
- **`SplatKit encode compute`**: CPU cost of recording the cull and sort dispatches.
  The rest of `SplatKit frame` outside its three stages is draw recording and engine work.
- **`pacing`**: intervals between submits.
  Intervals alternating between one and two display periods, with many hitches, mean frames miss vsync because GPU time is near or over the budget.
- **`render_thread`**: high `Running` means CPU-bound, and high `R` or `R+` means it waits for a core.
  Running mostly on the slowest cluster means the scheduler parked it on little cores.
- **`cpu_clusters`**: a mean clock far below max on the render thread's cluster means throttling.
  So does gpu ms rising across the 30-second windows in `logcat.txt`.
  A throttled run measures heat: cool the phone and rerun.
- **`memory`**: process RSS.
  GPU memory is not in the trace on Android 11.
- **`load_ms`**: world decode, collider decode and GPU upload of the last load.

## Validation

`--validation` builds the debug dev app, which loads the Khronos validation layer from `apps/android-dev/app/src/debug/jniLibs` ([scripts/fetch-validation-layers.sh](../../../scripts/fetch-validation-layers.sh)).
The run fails when logcat lacks `validation layers: on` or carries any `validation:` message; `report.json` `errors` holds the messages.
Its timings come from a debug build and compare with nothing.

## Limits

- Perfetto sees CPU time, scheduling, clocks and memory; GPU time comes only from the app's timestamp queries.
  Per-draw GPU counters (fragments shaded, overdraw, ALU, bandwidth) come from a Snapdragon Profiler Snapshot of a debug build, which is valid for counts and never for milliseconds.
- `--pose` keeps the position; the turn resets yaw and pitch.
- A run without `app slices in trace: True` lost the app's trace sections: the installed APK must be the profileable release build the script makes.
