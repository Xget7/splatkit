# GPU comparisons

Compare revisions on the same physical device/backend.
Before proposing or delegating an optimization, complete the [prior-results review](gpu-refs/README.md#before-implementation).
Use [capture guidance](GPU_CAPTURE.md) for GPU timelines, frame analysis and power; [validation](VALIDATION.md#pr-benchmark-evidence) defines acceptance and PR evidence.

## Prepare

Copy [reference-12m.json](../scripts/benchmarks/reference-12m.json) under ignored `build/` and fill in the device, prepared build paths and full source commits.
The preset owns the world, pose, policy, warmup and A-B-B-A schedule.
Use the [12M reference world](VALIDATION.md#reference-performance-world); verify framing and record backend-specific quality, since matching preset names do not imply matching images.

Build each variant once, serialize shared build directories and transfer the world before running.
The runner checks executable/APK hashes, reuses consecutive installs and retains failed-run artifacts.
Use a new output directory for each experiment.

## Run

Use `metal` or `vulkan` as the target:

```sh
python3 scripts/gpu_benchmark.py plan --config build/experiment.json --target vulkan
python3 scripts/gpu_benchmark.py run --config build/experiment.json --target vulkan --out output/gpu/vulkan-timing
python3 scripts/gpu_benchmark.py compare --manifest output/gpu/vulkan-timing/experiment.json
```

Timing uses Release, validation off, matched viewport/policy and comparable starting thermal conditions.
Preserve repeat spread; a difference within that spread establishes no gain.
Run diagnostic captures separately from timing.
The runner's `--help` describes phase options; [optional plotting dependencies](../scripts/requirements-benchmark.txt) are unnecessary for JSON/Markdown comparisons.

## Report

Keep the experiment manifest, per-run reports/logs/traces and the app's [run file](../scripts/benchmarks/run.schema.json) as `run.json`, alongside the offline comparison outputs.
Follow the [evidence contract](VALIDATION.md#pr-benchmark-evidence): before/after tables, device/build provenance, workload comparability and links to raw artifacts.
Label combined intervals and unavailable counters explicitly; app timings alone do not establish GPU pass analysis.
Record the outcome, including regressions or no gain, in the [backend findings](gpu-refs/README.md#recording-results).
