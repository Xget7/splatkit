# Repeatable GPU profiling

Use the same experiment contract on physical Metal and Vulkan devices, comparing revisions within one backend/device.
[reference-12m.json](../scripts/benchmarks/reference-12m.json) is the parameter source; [gpu_benchmark.py](../scripts/gpu_benchmark.py) plans, runs prepared builds and summarizes repeats offline.
Do not compare the backends' `high` presets as equivalent image quality or use a captured/replayed frame as sustained phone performance.

| Parameter | Reference |
|---|---|
| World | [12M fixture](VALIDATION.md#reference-performance-world), 13,375,264 prepared nodes |
| Camera | Verified room pose `-7.25,1.6,5.25,0,0`; fixed-position 360-degree turn |
| Policy | 1.8M budget, subpixel 1; backend-specific quality recorded |
| Timing | A-B-B-A, 2 x 30 s per build; 10 s warmup per variant |
| Provenance | Explicit device, declared full source commit, installed executable/APK SHA-256 |
| Outputs | `experiment.json`, per-run report/log/trace and the app's [run file](../scripts/benchmarks/run.schema.json) as `run.json`, offline `comparison.json` / `comparison.md` |

Copy the preset under ignored `build/`, fill the physical device and prepared build paths/commits, and verify framing before timed runs.
Build each variant once, serializing shared build directories; the suite never builds, checks out revisions or transfers the large world.
A full timing plan uses 140 seconds of active turns; installation, world loading, cooldown and trace saving add wall time.
Consecutive runs reuse the same install; variant changes reinstall, and prepared executable/APK hashes are checked before every run.
Failed runs stop the suite and retain their artifacts; output directories are never reused.

Planning, execution and Markdown comparisons use the standard library; install `scripts/requirements-benchmark.txt` only for optional offline plots.

```sh
python3 scripts/gpu_benchmark.py plan --config build/experiment.json --target metal
python3 scripts/gpu_benchmark.py plan --config build/experiment.json --target vulkan
python3 scripts/gpu_benchmark.py run --config build/experiment.json --target metal --out output/gpu/metal-timing
python3 scripts/gpu_benchmark.py run --config build/experiment.json --target vulkan --out output/gpu/vulkan-timing
python3 scripts/gpu_benchmark.py compare --manifest output/gpu/metal-timing/experiment.json
```

## Capture once, analyze offline

1. **Timing:** use Release, validation off, matched policy/viewport and comparable starting thermal conditions.
   Vulkan's existing runner records Perfetto on each run; Metal's timing phase enables neither Instruments nor frame capture.
   Preserve repeat spread; a mean inside that spread establishes no gain.
2. **Timeline:** run `--phase timeline` on both prepared variants with the same scenario.
   Metal uses `xctrace` / Metal System Trace; Vulkan uses Perfetto with the app's frame/load/CPU encoding slices and requests registered `gpu.renderstages` producers, using their exact vendor-suffixed names.
   The query is archived as `gpu-sources.txt`; registration alone does not establish measured GPU samples.
   Offline analysis lists actual native GPU slices when present, labelled device-wide rather than falsely attributed to this app.
   Export and inspect GPU work, queue overlap and waits where the trace supports them; CPU encoding slices are not GPU pass durations.
   `xctrace export --input metal.trace --toc` lists exportable tables; export relevant schemas with `--xpath` and retain the raw trace.
   The suite records one turn per variant, outside the timing comparison; trace saving over Wi-Fi can dominate wall time.
3. **Frames:** select a specific observed event from the timeline, then capture one or a few frames at that event in each variant.
   Record frame/boundary ID, heading, selected/drawn counts, capture scope, timestamp and instrumentation; matching elapsed seconds alone does not match adaptive-LOD work.
   Metal: `gpucapture` can attach and save `.gputrace`; `gpudebug` can inspect/replay it and return JSON.
   Vulkan: AGI frame profiling captures API commands, pipelines and resources; require a compatible device/driver and profiling-enabled build.
   Frame capture/replay is diagnostic and must stay outside the Release timing comparison.
4. **Power:** for a power-relevant change, run Metal `--phase power` unplugged with matched brightness and thermal state.
   The runner exports Power Profiler tables; inspect charging, range and actual populated power lanes before claiming battery/energy changes.
   Android power rails/counters require device support and a dedicated configuration; this suite rejects Android's power phase rather than substituting FPS or CPU frequency.
5. **Engineer handoff:** write the [before/after tables](VALIDATION.md#pr-benchmark-evidence), correlate events in a timestamped table and link logs, raw traces, frame captures and source JSON.
   Compare isolated visibility, selection, radix, raster and present where native captures resolve them; otherwise label combined intervals explicitly.
   Record unavailable bandwidth/counters as unmeasured; attach large native captures to the PR/workflow artifacts instead of source exports.

## Native frame tools and current limits

On this Mac, `gpucapture`, `gpudebug` and `metalperftrace` are installed.
`gpucapture --list-devices` and `gpudebug --list-devices` require Xcode running for remote-device discovery; their device IDs are not the iPhone UDID.
Inspect `gpucapture list --help` and `gpucapture boundaries --help`, find the intended process/boundary and pass those actual IDs:

```sh
gpucapture start --device <capture-device-id> --pid <pid> --boundary <boundary-id> --count 1 --output frame.gputrace
gpudebug --gputrace frame.gputrace --device <replay-device-id> --json
```

Prefer device-wide scope when the frame spans selection, compute and render queues; a present boundary alone does not prove every queue was captured.
Programmatic capture through `MTLCaptureManager` is an alternative when deterministic in-app event hooks are required; enable capture only in the profiling build.
The benchmark suite does not yet schedule frame capture or extract per-pass hardware counters.
Its current automation covers prepared-build timing, native timeline capture, iOS power capture and descriptive JSON comparison and automatic native Markdown comparison tables.
No Android device was connected for this implementation; no native frame capture was validated on the iPhone.
The four PR #60 runs used app-published timings, not detailed GPU timelines or frame captures.

Sources: [Apple Metal tools](https://developer.apple.com/metal/tools/), [programmatic capture](https://developer.apple.com/documentation/xcode/capturing-a-metal-workload-programmatically), [AGI quickstart](https://developer.android.com/agi/start), [Perfetto GPU sources](https://perfetto.dev/docs/data-sources/gpu).
