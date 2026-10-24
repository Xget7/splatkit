# Validation

No publication until artifact integration, matched-image review and sustained physical-device runs pass.
Mac Metal execution and Android cross-compilation do not establish phone performance.

## Local checks

```sh
python3 scripts/sdk_harness.py check engine
python3 scripts/sdk_harness.py check metal
python3 scripts/sdk_harness.py check android
python3 -m pip install -r scripts/requirements-validation.txt
python3 -m unittest discover -s scripts/tests
```

Capture comparisons need Python 3.10+ and Pillow; their tests explicitly skip if Pillow is absent.
Set `SPLAT_FIXTURES_DIR` for external core fixtures; report skips, never count them as validation.

## Matched images

```sh
python3 scripts/compare_captures.py reference.json lod.json
```

Each manifest records `image` (relative path), `image_sha256`, original `world_sha256`, `environment`, `camera` and `settings`.
`camera` contains column-major `view`/`projection` matrices (16 numbers each) and integer `viewport: [width, height]`.
`settings` includes `render_scale`, `sh_degree`, `linear_blending`, `splat_budget` and every other active rendering override.
Only `splat_budget` may differ by default; list other deliberate differences with `--vary`.
Use HUD-free captures; `--roi X Y WIDTH HEIGHT` explicitly restricts measurement to a matching region.
The tool rejects mismatched cameras, dimensions, hashes and colour tags; it never aligns or resizes images.
It reports RGB MAE, PSNR, p99 error and worst 32-pixel region; `--max-mae` is an explicit numeric gate, not perceptual acceptance.
Review silhouettes, thin geometry, transparency and temporal LOD transitions separately.

The Mac integration test `MetalLodSelectionTest.OfflineFixtureRendersWithBoundedDrawCount` accepts `SPLAT_LOD_PATH`, optional `SPLAT_LOD_REFERENCE_SPZ`, `SPLAT_LOD_BUDGET` and `SPLAT_LOD_CAPTURE`.
`SPLAT_LOD_POSE="x y z yaw pitch"` takes the pose as the SplatKit frame log prints it; without it the framing is the historical ISS view.
`SPLAT_LOD_SIZE="width height"` and `SPLAT_LOD_ERROR_PIXELS` match the device's drawable and policy; `SPLAT_LOD_FRAMES` lets the adaptive threshold settle before the last frame is captured.
An offline hierarchy is not interchangeable evidence for the default load-time hierarchy.

## Sustained runs

```sh
python3 scripts/benchmark_report.py capture.log --environment physical --pid 123 --minimum-seconds 600
```

The shared benchmark accepts finite durations in `(0, 3600]`, preserves wall-time stalls, emits 30-second windows and final p99.
It still makes one full turn per run: a 600-second turn is not the same trajectory speed as a 30-second turn.
For constant-speed thermal comparisons, repeat identical 30-second turns for 10-15 minutes and keep every run plus the continuous thermal/memory trace; use `--minimum-seconds 30` per run.
Pin world/hierarchy hashes, build, driver, pose, viewport, SH, depth mode and quality settings; warm up before capturing, record charging state and lifecycle interruptions.
Sample PSS/RSS and thermal status independently; stop the device test on severe thermal status, repeated GPU failures or an unresponsive UI.
GPU timings are latest-completed-query samples, may repeat and are not guaranteed to match the current host frame; zero/unavailable is not zero cost.
Historical logs cannot supply missing p99 or memory/thermal evidence.
Host callbacks may remain display-paced even when presentation vsync is disabled.

## PR benchmark evidence

Screen changes that plausibly affect FPS, frame/GPU time or memory bandwidth with a short matched before/after benchmark on the affected physical device/backend.
A measured change of roughly 10-20% or more, beyond run-to-run variation, requires visual evidence, whether improvement or regression.
A meaningful frame-budget crossing or bandwidth change also qualifies; the percentage is a screening guide, not an exemption.
For unrelated changes or effects within noise, state why benchmarking or a chart is unnecessary; do not spend phone time producing an irrelevant benchmark.

The PR's Evidence must show the engineer a rendered before/after chart, embedded in the body, with a one-sentence takeaway.
Use English, a pure-black background, white text and a restrained accent; remove redundant labels and keep secondary provenance in the linked evidence.
Each panel must answer a different question; choose line charts for time, bars for comparisons, clear units and enough contrast to distinguish the data.
Correlate metric changes with recorded application events on the shared timeline: label meaningful actions or transitions and carry their markers across panels, rather than showing seconds alone.
Link each marker to its log/trace evidence; distinguish logged events, sampled observations and hypotheses, and disclose clock alignment or sampling delay instead of implying causality.
For a benchmark report or handoff outside a PR, present the same chart directly to the engineer in charge.
Use a standard plotting tool to save a standalone PNG or SVG beside the benchmark evidence; a table or link to raw logs alone does not satisfy the visual requirement.
Label baseline/change commits, device/backend, workload, metric and units; show absolute values, percentage change and direction of improvement.
Show individual repeated runs or their spread, and keep different units on separate panels rather than a misleading shared axis.
Include FPS and frame/GPU milliseconds when available; show bandwidth when the change concerns memory traffic and counters support it, and power when it is measured and relevant.
Use the same device, world, camera speed, viewport, render policy and comparable image quality and starting thermal state; state deliberate differences.
Run at least two short runs per side, alternating A/B when practical; reuse builds, installs and fixtures, then generate the chart offline.
Link the source JSON/logs/traces and record physical-device provenance, build identity, profiling overhead and sampling limits.
Label estimated bandwidth or power explicitly; unavailable counters are unmeasured, never zero or inferred from FPS alone.
If hardware or counters are unavailable, report the missing check and leave the performance conclusion unvalidated instead of inventing a chart.

## iOS power comparisons

Requires a physical iPhone on iOS 26+, wireless debugging and no cable or MagSafe charging.
Apple reports overall system power as zero while charging; check charging, brightness and thermal lanes in Instruments.
See [Power Profiler](https://developer.apple.com/documentation/xcode/measuring-your-app-s-power-use-with-power-profiler).
The runner uses Blank plus the Power Profiler instrument; the stock template trapped in `NetworkConnectionStatsModeling` on Instruments 27.1 during physical iPhone recording finalization.

Build and install once, using a short unrecorded turn to check the world and pose:

```sh
python3 scripts/ios_perf.py --udid <udid> --world kitchen_500k.spz --seconds 30
```

Unplug, keep brightness fixed, let the phone cool, then capture one 30-second probe:

```sh
python3 scripts/ios_perf.py --udid <udid> --world kitchen_500k.spz --seconds 30 --power --skip-build --skip-install --out output/ios-perf/power-probe
```

Each output directory must be new.
The recorder finishes its time limit and saves before the runner terminates its attached app.
After the benchmark the view is still and the engine draws only on changes.
Saving over Wi-Fi can still take minutes; export runs offline after app termination.
Inspect `power.trace` in Instruments first; `power-export/toc.xml`, table XML and `manifest.json` preserve exportable data and original units.
`report.json` records capture/export status separately from power metrics, which remain unvalidated until their schema, units and time range are checked on a physical capture.
The recorder attaches after the benchmark start log, so initial frames may be missing; compare a common covered interval, not whole-turn energy divided by the full frame count.
Empty or partially failed exports fail the run and preserve the trace for an offline retry without using the phone:

```sh
python3 scripts/ios_power.py output/ios-perf/power-probe/power.trace --out output/ios-perf/power-probe/export-retry
```

Power tables with no rows are reported as unavailable, never as zero consumption.
An exported table alone does not establish usable power samples.

Once the probe is usable, repeat identical 30-second turns in A/B/B/A order, with a new output directory per turn and comparable starting temperature.
Change one supported runtime option (`--budget` or `--subpixel`) to avoid rebuilding; these can change image quality and require matched-image review.
Keep world, pose, quality, brightness and orbit duration fixed; the runner does not enforce a common FPS target, so record achieved FPS with consumption.
Compare battery percentage points per hour and CPU/GPU impact in their exported units on the same phone; impact scores are not watts or joules.
Short runs screen for large differences; increase repetitions if their variation overlaps, and keep sustained thermal claims separate.
No physical power capture or export-schema validation is established by host tests.

## Remaining parity gates

| Contract | Metal / iOS | Vulkan / Android |
|---|---|---|
| Native LOD, visibility, radix, indirect draw | Implemented | Implemented |
| Source and completed draw counts | Public stats | Public stats |
| First successful GPU world-frame callback | Public delegate | Public `onWorldFrameReady` listener |
| Scripted look-at with explicit up vector | Public API | Public API |
| Orbit: anchor, dolly, focus, animated turn | Public API; scripted 360 on a physical iPhone | Public API |
| Quality presets | Individual properties | `RenderQuality` presets |
| GPU sort timing | Visibility + radix command interval | Isolated radix interval |
| Per-instance render policy and capabilities | `SplatMetalView.renderPolicy`, `deviceCapabilities` | `SplatSurfaceView.applyRenderPolicy`, `deviceCapabilities` |
| Policy fields applied natively | Sort depth, LOD error and LOD splat limit with GPU sort; sub-pixel on both order paths | Sort depth, LOD error and LOD splat limit with GPU visibility; sub-pixel on both order paths |
| RN policy prop, capability and policy events | Physical iPhone 17 Pro | Physical Mi 9: revisions, fallback warnings, preset reloads, background; rejection host-tested only |

Shared functionality does not imply identical internal algorithms or timing semantics.
PNG capture is a Metal validation aid.
Consistent stage diagnostics remain open; there is no equivalent-feature or universal 30/60 FPS claim.
