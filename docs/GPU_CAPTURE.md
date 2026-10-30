# GPU diagnostic captures

Use after a [timing comparison](GPU_PROFILING.md) identifies an effect that needs explaining.
Capture both variants with the same scenario, outside Release timing runs.
Capture/replay is diagnostic evidence, not sustained phone performance.

## Timeline

Run `gpu_benchmark.py run --phase timeline` with the prepared comparison config.
Metal records Metal System Trace through `xctrace`; Vulkan records Perfetto app slices and requests registered `gpu.renderstages` producers.

Check actual GPU samples before attributing costs:

- `gpu-sources.txt` records producer discovery, not proof of populated GPU data.
- Device-wide GPU slices are not automatically attributable to this app.
- CPU encoding slices are not GPU pass durations.
- Export GPU work, queue overlap and waits only where the trace resolves them; retain raw traces.

For Metal, `xctrace export --input metal.trace --toc` lists exportable tables; select relevant schemas with `--xpath`.
Trace saving over Wi-Fi can dominate wall time.

## Frames

Choose an observed timeline event, then capture one or a few frames in each variant.
Record frame/boundary ID, heading, selected/drawn counts, scope, timestamp and instrumentation.
Matching elapsed time alone does not match adaptive-LOD work.

### Metal

Use `gpucapture` to save `.gputrace` and `gpudebug` to inspect/replay it.
Remote discovery requires Xcode running; capture/replay device IDs differ from the iPhone UDID.
Find the actual device, process and boundary using `--list-devices`, `list --help` and `boundaries --help`:

```sh
gpucapture start --device <capture-device-id> --pid <pid> --boundary <boundary-id> --count 1 --output frame.gputrace
gpudebug --gputrace frame.gputrace --device <replay-device-id> --json
```

Prefer device-wide scope when selection, compute and render span queues; a present boundary alone does not prove all queues were captured.
[Programmatic capture](https://developer.apple.com/documentation/xcode/capturing-a-metal-workload-programmatically) is an alternative for deterministic event hooks; enable it only in profiling builds.

### Vulkan

[AGI frame profiling](https://developer.android.com/agi/start) captures commands, pipelines and resources on compatible devices/drivers with a profiling-enabled build.
Report unavailable frame capture or hardware counters explicitly.

## Power

Metal supports `--phase power`; follow [iOS power comparisons](VALIDATION.md#ios-power-comparisons) for unplugged operation, matched brightness/thermal state and populated Power Profiler lanes.
Android requires supported power rails/counters and separate configuration; this runner rejects Android power capture.
FPS and CPU frequency are not energy measurements.

## Evidence limits

The runner automates timing, timelines, iOS power capture and comparison tables, but not frame capture or per-pass hardware counters.
Compare visibility, selection, radix, raster and present separately only where captures resolve them; label combined intervals otherwise.
Link captures and timestamped event correlations in the [PR evidence](VALIDATION.md#pr-benchmark-evidence); attach large captures as artifacts rather than source exports.
The historical PR #60 runs used app-published timings, not detailed GPU timelines or frame captures.

Sources: [Apple Metal tools](https://developer.apple.com/metal/tools/), [Perfetto GPU sources](https://perfetto.dev/docs/data-sources/gpu).
