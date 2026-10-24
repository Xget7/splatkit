---
name: ios-perf
description: Benchmark runs of the iOS Metal renderer on a physical iPhone, with optional Metal System Trace. Use to record a baseline, compare runs before and after a renderer change, or capture a trace to split GPU passes.
---

# iOS performance

A **run** is one release build of the dev app doing its benchmark turn on an iPhone: [scripts/ios_perf.py](../../../scripts/ios_perf.py) installs it, runs the turn through `devicectl` with the HUD off, and writes one run directory under `output/ios-perf/`.
Its `--help` carries the flags.
Use the [12M reference world](../../../docs/VALIDATION.md#reference-performance-world) for physical acceptance and benchmarks; always pass `--world scene.lodsplat`, never substitute kitchen for representative evidence.
Before reporting or opening a PR, follow [visual benchmark evidence](../../../docs/VALIDATION.md#pr-benchmark-evidence): material effects require an embedded before/after chart for the reviewing engineer, backed by the run files.
