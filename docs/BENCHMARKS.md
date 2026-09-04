# Benchmark log

Every optimisation starts with a measurement of the state before it, recorded here, and ends with the same measurement after.
Device: Xiaomi Mi 9, Adreno 640, Vulkan 1.1.128, Android 11, release build, phone cooled below 48 C before each run.
Command: `adb shell am start -n com.splatkit.devapp/.MainActivity --es world <file> --ez benchmark true [--ef scale S] [--ef seconds N]`, one full turn in place, GPU time from timestamp queries.

| Date | Commit | Scene | Settings | GPU ms mean / p50 / p95 | Sort ms | Cull ms | Drawn of total | Load: decode / reorder / upload ms |
|---|---|---|---|---|---|---|---|---|
| 2026-09-04 | 91b8d76 | house 2M | scale 1.0 | 32.4 / 32.0 / 41.1 | 41 to 46 | 10 | 143k to 291k of 2M | 530 / 470 / 270 |
| 2026-09-04 | 91b8d76 | raccoon 932k, SH 3 | scale 1.0 | 14.4 / 12.9 / 27.8 | 95 | n/a | up to 932k | 634 / 216 / 501 |
| 2026-09-04 | 91b8d76 | raccoon 932k, SH 0 | scale 1.0 | 13.9 / 12.4 / 26.7 | 95 | n/a | up to 932k | |

| 2026-09-04 | LoD tree | house 2M, budget 500k | scale 1.0, budget spread over the sphere | 25.6 / 21.7 / 45.7 | 18 | 13 | 18k to 52k of 500k | tree build 4.3 s on the Mac, blurry: rejected |
| 2026-09-04 | LoD tree | house 2M, budget 500k | scale 1.0, budget weighted ahead (0.02 behind) | 32.4 / 32.5 / 42.4 | 16 | 14 | 152k of 500k | select 91 ms, still soft |
| 2026-09-04 | LoD tree | house 2M, budget 1M | scale 1.0, budget weighted ahead | 31.1 / 32.1 / 41.3 | 34 | 14 | 239k of 1M | select 218 ms, looks like the full scene |
| 2026-09-04 | LoD tree | house 2M, no budget | scale 1.0 | 32.3 / 31.9 / 40.9 | 44 | 10 | 149k to 289k of 2M | reference |

Reading: at full resolution the house draws 150k to 290k splats and nearly all of them are larger than a pixel, so a level of detail tree has nothing to merge in view.
A budget below what the view needs blurs the image without saving GPU time, because the merged nodes are bigger and cost the fragments the leaves would have.
The tree pays off when the scene is bigger than what the view needs at a pixel each (far content, many millions of splats) or at low quality modes; it is off by default.

Earlier numbers, before this log existed, are in the roadmap tables.
