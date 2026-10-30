# GPU evidence

Read the target backend's findings before proposing, implementing or delegating a GPU optimization.

| Need | Read |
|---|---|
| Vulkan capabilities and device findings | [Vulkan](vulkan/README.md), including [Mi 9 saturation masking](vulkan/README.md#front-to-back-saturation-masking) |
| Metal capabilities and limits | [Metal](metal/README.md) |
| Historical measurements and rejected experiments | [Benchmarks](../BENCHMARKS.md) |
| Pipeline and stage source links | [Architecture](../ARCHITECTURE.md) |
| Repeatable comparisons | [Profiling](../GPU_PROFILING.md) |

## Before implementation

1. Read the relevant findings, benchmarks and stage comments, including unsuccessful approaches.
2. State the prior result and its evidence limits, what differs in this proposal, and the measurement that could justify it.
   Revisit a rejected approach only with a concrete difference in device, driver, workload or implementation; otherwise retain the established path.
3. Carry those three items into the implementation handoff or PR.
   The review is complete when they are explicit; no separate planning document is needed.

## Recording results

Keep one finding in the backend's README; link measurements and artifacts instead of copying them.
Each finding records:

- Technique and disposition: retained, rejected for the tested conditions, or unvalidated.
- Device/driver, workload/settings and observed result.
- Evidence source, missing evidence and suspected causes, separately labelled.
- What would justify revisiting it.

Distinguish measured results, user reports and code-comment history.
Record regressions and no gain as well as improvements; missing raw data remains explicit.
Build and correctness checks do not overturn a physical-device performance result.
