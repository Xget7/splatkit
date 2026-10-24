---
name: android-perf
description: Perfetto performance runs of the Android Vulkan renderer on a physical phone. Use to record a baseline, analyze a trace, or compare runs before and after a renderer change.
---

# Android performance with Perfetto

A **run** is one release build of the dev app doing its benchmark turn under Perfetto: [scripts/android_perf.py](../../../scripts/android_perf.py) records it and [scripts/perfetto_analysis.py](../../../scripts/perfetto_analysis.py) reads the trace.
Both scripts' `--help` carry the flags.
Use the [12M reference world](../../../docs/VALIDATION.md#reference-performance-world) for physical acceptance and benchmarks; pass its installed filename explicitly and never substitute kitchen for representative evidence.
Before reporting or opening a PR, follow [visual benchmark evidence](../../../docs/VALIDATION.md#pr-benchmark-evidence): material effects require an embedded before/after chart for the reviewing engineer, backed by the run files.
