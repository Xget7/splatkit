#!/usr/bin/env python3
"""Summarize a SplatKit Android Perfetto trace: the Vulkan frame on the render thread, pacing,
CPU placement and clocks, memory and world load.

Needs the perfetto package (scripts/requirements-perf.txt), installed in build/perf-venv:
  python3 -m venv build/perf-venv && build/perf-venv/bin/pip install -r scripts/requirements-perf.txt
  build/perf-venv/bin/python scripts/perfetto_analysis.py TRACE [--json OUT]
"""

import argparse
import json
import math
from pathlib import Path
import sys

PACKAGE = "com.splatkit.devapp"
RENDER_THREAD = "SplatKitRender"
LOADER_THREAD = "SplatKitLoader"
# Slice names from packages/splatkit-android/src/main/cpp/engine/TraceSection.h.
SLICE_PREFIX = "SplatKit "
FRAME = "SplatKit frame"
WAIT_FOR_FRAME_SLOT = "SplatKit wait for frame slot"
ENCODE_COMPUTE = "SplatKit encode compute"
SUBMIT_PRESENT = "SplatKit submit and present"
FRAME_STAGES = (WAIT_FOR_FRAME_SLOT, ENCODE_COMPUTE, SUBMIT_PRESENT)
UPLOAD_WORLD = "SplatKit upload world"
SAVE_PIPELINE_CACHE = "SplatKit save pipeline cache"
DECODE_WORLD = "SplatKit decode world"
DECODE_COLLIDER = "SplatKit decode collider"
BENCHMARK = "SplatKit benchmark"
LOAD_SLICES = (DECODE_WORLD, DECODE_COLLIDER, UPLOAD_WORLD, SAVE_PIPELINE_CACHE)
RENDER_THREAD_LOAD_SLICES = (UPLOAD_WORLD, SAVE_PIPELINE_CACHE)
CPU_FREQUENCY_TRACK = "cpufreq"
RSS = "mem.rss"
RSS_COUNTERS = (RSS, "mem.rss.anon")
RUNNING = "Running"
STATUS_OK = "ok"
STATUS_NO_WINDOW = "no_steady_window"
WINDOW_BENCHMARK = "benchmark"
WINDOW_AFTER_UPLOAD = "after_upload"
NS_PER_MS = 1e6
NS_PER_S = 1e9
PERCENT = 100
KHZ_PER_MHZ = 1000
BYTES_PER_MIB = 1 << 20
# A frame interval this many times the median counts as a hitch.
HITCH_FACTOR = 1.5
PERCENTILES = (50, 95, 99)


def percentile(values, pct):
    """Nearest-rank percentile; None for no values."""
    if not values:
        return None
    ordered = sorted(values)
    rank = max(1, math.ceil(pct / PERCENT * len(ordered)))
    return ordered[rank - 1]


def distribution_ms(durations_ns):
    """Count, mean, percentiles and max of nanosecond durations, in milliseconds."""
    if not durations_ns:
        return {"count": 0}
    ms = [d / NS_PER_MS for d in durations_ns]
    result = {"count": len(ms), "mean_ms": sum(ms) / len(ms), "max_ms": max(ms)}
    result.update({f"p{pct}_ms": percentile(ms, pct) for pct in PERCENTILES})
    return result


def clusters_by_max_frequency(max_khz_by_cpu):
    """Groups CPUs that share a maximum frequency, slowest first: little, big, prime on most SoCs."""
    groups = {}
    for cpu, khz in sorted(max_khz_by_cpu.items()):
        groups.setdefault(khz, []).append(cpu)
    return [{"cpus": cpus, "max_mhz": khz / KHZ_PER_MHZ} for khz, cpus in sorted(groups.items())]


def pacing(starts_ns):
    """Intervals between consecutive submits; hitches exceed HITCH_FACTOR times the median."""
    intervals = [b - a for a, b in zip(starts_ns, starts_ns[1:])]
    summary = distribution_ms(intervals)
    median = percentile(intervals, 50)
    summary["hitches"] = sum(1 for i in intervals if median and i > HITCH_FACTOR * median)
    return summary


class Trace:
    def __init__(self, path, pid=None):
        from perfetto.trace_processor import TraceProcessor
        self.tp = TraceProcessor(trace=str(path))
        # An app launched after recording starts can stay named <pre-initialized>: Perfetto read
        # its /proc entry between the zygote fork and the rename, so the pid is the reliable match.
        self.process = f"p.pid = {int(pid)}" if pid else f"p.name = '{PACKAGE}'"

    def rows(self, sql):
        return [row.__dict__ for row in self.tp.query(sql)]

    def thread_slices(self, thread, name, window=None):
        clause = f"and s.ts >= {window[0]} and s.ts + s.dur <= {window[1]}" if window else ""
        return self.rows(f"""
            select s.ts, s.dur from slice s
            join thread_track tt on s.track_id = tt.id join thread t using (utid)
            join process p using (upid)
            where t.name = '{thread}' and {self.process} and s.name = '{name}' {clause}
            order by s.ts""")

    def benchmark_window(self):
        """The async slice the app keeps open over a benchmark turn, if the trace saw it end."""
        rows = self.rows(f"""
            select s.ts, s.dur from slice s join process_track pt on s.track_id = pt.id
            join process p using (upid)
            where {self.process} and s.name = '{BENCHMARK}' and s.dur > 0
            order by s.ts desc limit 1""")
        return (rows[0]["ts"], rows[0]["ts"] + rows[0]["dur"]) if rows else None

    def steady_window(self):
        """Without a benchmark slice: from the end of the world upload to the last frame."""
        upload = self.thread_slices(RENDER_THREAD, UPLOAD_WORLD)
        frames = self.thread_slices(RENDER_THREAD, FRAME)
        if not upload or not frames:
            return None
        start = upload[-1]["ts"] + upload[-1]["dur"]
        end = frames[-1]["ts"] + frames[-1]["dur"]
        return (start, end) if end > start else None

    def thread_states(self, window):
        # The thread that draws the frames: threads it starts, such as the validation layer's,
        # inherit its name.
        return self.rows(f"""
            select ts.state, ts.cpu, sum(min(ts.ts + ts.dur, {window[1]}) - max(ts.ts, {window[0]})) dur
            from thread_state ts
            where ts.utid in (
                select tt.utid from slice s join thread_track tt on s.track_id = tt.id
                join thread t using (utid) join process p using (upid)
                where t.name = '{RENDER_THREAD}' and {self.process} and s.name = '{FRAME}')
              and ts.ts < {window[1]} and ts.ts + ts.dur > {window[0]}
            group by ts.state, ts.cpu""")

    def cpu_max_khz(self):
        return {r["cpu"]: r["khz"] for r in self.rows(f"""
            select cct.cpu, max(c.value) khz from counter c
            join cpu_counter_track cct on c.track_id = cct.id
            where cct.name = '{CPU_FREQUENCY_TRACK}' group by cct.cpu""")}

    def cpu_mean_khz(self, window):
        """Time-weighted mean frequency per CPU over the window."""
        return {r["cpu"]: r["khz"] for r in self.rows(f"""
            with f as (
              select cct.cpu, c.value, c.ts,
                     lead(c.ts, 1, {window[1]}) over (partition by cct.cpu order by c.ts) next_ts
              from counter c join cpu_counter_track cct on c.track_id = cct.id
              where cct.name = '{CPU_FREQUENCY_TRACK}')
            select cpu, sum(value * (min(next_ts, {window[1]}) - max(ts, {window[0]})))
                        / sum(min(next_ts, {window[1]}) - max(ts, {window[0]})) khz
            from f where next_ts > {window[0]} and ts < {window[1]} group by cpu""")}

    def process_memory(self, window, counter):
        rows = self.rows(f"""
            select max(c.value) peak, avg(c.value) mean from counter c
            join process_counter_track pct on c.track_id = pct.id join process p using (upid)
            where {self.process} and pct.name = '{counter}'
              and c.ts between {window[0]} and {window[1]}""")
        if not rows or rows[0]["peak"] is None:
            return None
        return {"peak_mib": rows[0]["peak"] / BYTES_PER_MIB, "mean_mib": rows[0]["mean"] / BYTES_PER_MIB}


def render_thread_breakdown(states, clusters, window):
    """Share of the window the render thread spends running per cluster, runnable or blocked."""
    span = window[1] - window[0]
    cluster_of = {cpu: index for index, c in enumerate(clusters) for cpu in c["cpus"]}
    running = [0] * len(clusters)
    other = {}
    for row in states:
        if row["state"] == RUNNING and row["cpu"] in cluster_of:
            running[cluster_of[row["cpu"]]] += row["dur"]
        else:
            other[row["state"]] = other.get(row["state"], 0) + row["dur"]
    return {
        "running_pct_by_cluster": [{"cpus": c["cpus"], "pct": PERCENT * r / span} for c, r in zip(clusters, running)],
        "running_pct": PERCENT * sum(running) / span,
        # R and R+ wait for a CPU; S sleeps (fence or present waits); D is blocked in the kernel.
        "state_pct": {state: PERCENT * dur / span for state, dur in sorted(other.items())},
    }


def analyze(path, pid=None):
    trace = Trace(path, pid)
    window = trace.benchmark_window()
    window_source = WINDOW_BENCHMARK if window else WINDOW_AFTER_UPLOAD
    window = window or trace.steady_window()
    if window is None:
        return {"status": STATUS_NO_WINDOW,
                "hint": f"no '{UPLOAD_WORLD}' then '{FRAME}' slices from {PACKAGE}; "
                        "is the app profileable, and is --pid right?"}
    window_s = (window[1] - window[0]) / NS_PER_S
    submits = trace.thread_slices(RENDER_THREAD, SUBMIT_PRESENT, window)
    stages = {name: distribution_ms([s["dur"] for s in trace.thread_slices(RENDER_THREAD, name, window)])
              for name in (FRAME,) + FRAME_STAGES}
    clusters = clusters_by_max_frequency(trace.cpu_max_khz())
    mean_khz = trace.cpu_mean_khz(window)
    for cluster in clusters:
        means = [mean_khz[cpu] for cpu in cluster["cpus"] if cpu in mean_khz]
        cluster["mean_mhz"] = sum(means) / len(means) / KHZ_PER_MHZ if means else None
    load = {}
    for name in LOAD_SLICES:
        thread = RENDER_THREAD if name in RENDER_THREAD_LOAD_SLICES else LOADER_THREAD
        slices = trace.thread_slices(thread, name)
        load[name] = slices[-1]["dur"] / NS_PER_MS if slices else None
    return {
        "status": STATUS_OK,
        "window": window_source,
        "window_s": window_s,
        "submitted_frames": len(submits),
        "submit_fps": len(submits) / window_s,
        "render_thread_slices": stages,
        "pacing": pacing([s["ts"] for s in submits]),
        "render_thread": render_thread_breakdown(trace.thread_states(window), clusters, window),
        "cpu_clusters": clusters,
        "memory": {name: trace.process_memory(window, name) for name in RSS_COUNTERS},
        "load_ms": load,
        "unavailable": "GPU frequency, GPU memory and thermal counters are not in the trace on kernels "
                       "without power/gpu_frequency, gpu_mem and thermal events; GPU timings come from "
                       "the app's timestamp queries in report.json",
    }


def describe(result):
    """Short text summary for a terminal."""
    if result["status"] != STATUS_OK:
        return f"analysis: {result['status']}: {result.get('hint', '')}"
    lines = [f"{result['window']} window {result['window_s']:.1f} s, {result['submitted_frames']} frames submitted "
             f"({result['submit_fps']:.1f} fps)"]
    for name, d in result["render_thread_slices"].items():
        if d["count"]:
            lines.append(f"  {name:<30} mean {d['mean_ms']:6.2f} ms  p95 {d['p95_ms']:6.2f}  max {d['max_ms']:6.2f}")
    p = result["pacing"]
    if p["count"]:
        lines.append(f"pacing: interval p50 {p['p50_ms']:.1f} ms, p95 {p['p95_ms']:.1f}, max {p['max_ms']:.1f}, "
                     f"{p['hitches']} hitches over {HITCH_FACTOR}x median")
    rt = result["render_thread"]
    placement = ", ".join(f"cpus {c['cpus'][0]}-{c['cpus'][-1]} {c['pct']:.0f}%" for c in rt["running_pct_by_cluster"])
    states = ", ".join(f"{s} {v:.0f}%" for s, v in rt["state_pct"].items())
    lines.append(f"{RENDER_THREAD}: running {rt['running_pct']:.0f}% ({placement}); {states}")
    clocks = ", ".join(f"cpus {c['cpus'][0]}-{c['cpus'][-1]} {c['mean_mhz'] or 0:.0f}/{c['max_mhz']:.0f} MHz"
                       for c in result["cpu_clusters"])
    lines.append(f"clocks mean/max: {clocks}")
    rss = result["memory"][RSS]
    if rss:
        lines.append(f"memory rss peak {rss['peak_mib']:.0f} MiB, mean {rss['mean_mib']:.0f} MiB")
    lines.append("load: " + ", ".join(f"{k.removeprefix(SLICE_PREFIX)} {v:.0f} ms"
                                      for k, v in result["load_ms"].items() if v is not None))
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--pid", type=int, help=f"the app's pid; defaults to matching {PACKAGE} by name")
    parser.add_argument("--json", type=Path, help="also write the full result here")
    args = parser.parse_args()
    result = analyze(args.trace, args.pid)
    if args.json:
        args.json.write_text(json.dumps(result, indent=2, allow_nan=False))
    print(describe(result))
    return 0 if result["status"] == STATUS_OK else 1


if __name__ == "__main__":
    sys.exit(main())
