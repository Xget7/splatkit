#!/usr/bin/env python3
"""Build the Android dev app as release, run its benchmark turn under Perfetto and summarize it.

Writes trace.perfetto-trace, logcat.txt and report.json to one run directory; open the trace
at https://ui.perfetto.dev. Needs one adb device; the phone must stay awake and unlocked.
"""

import argparse
import datetime
import json
from pathlib import Path
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
import benchmark_report  # noqa: E402
import perfetto_analysis  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
APP_DIR = ROOT / "apps" / "android-dev"
BUILD_VARIANT = "release"
GRADLE_TASK = ":app:assembleRelease"
APK = APP_DIR / "app" / "build" / "outputs" / "apk" / BUILD_VARIANT / f"app-{BUILD_VARIANT}.apk"
PACKAGE = perfetto_analysis.PACKAGE
ACTIVITY = f"{PACKAGE}/.MainActivity"
DEVICE_TRACE = "/data/misc/perfetto-traces/splatkit-perf.perfetto-trace"
RUNS_DIR = ROOT / "output" / "android-perf"
ANALYSIS = ROOT / "scripts" / "perfetto_analysis.py"
# Holds the perfetto package (scripts/requirements-perf.txt) that trace_processor needs.
PERF_PYTHON = ROOT / "build" / "perf-venv" / "bin" / "python"
PERF_SETUP = ("python3 -m venv build/perf-venv && "
              "build/perf-venv/bin/pip install -r scripts/requirements-perf.txt")
# Finding a frame slice proves the app's trace sections reached the trace.
FRAME_SLICE = perfetto_analysis.FRAME.encode()
BENCHMARK_DONE = "benchmark gpu ms:"
WORLD_FAILED = "World failed:"
# World decode and upload before the turn starts, plus the tail Perfetto keeps after it.
LOAD_ALLOWANCE_S = 25
POLL_S = 1.0
TRACE_BUFFER_KB = 131072
STATS_BUFFER_KB = 8192
PROCESS_POLL_MS = 1000
MEMINFO_PERIOD_MS = 1000
FTRACE_EVENTS = (
    "sched/sched_switch", "sched/sched_waking", "power/cpu_frequency", "power/cpu_idle",
    "power/gpu_frequency", "power/suspend_resume", "thermal/thermal_temperature",
)
ATRACE_CATEGORIES = ("gfx", "view", "am", "wm", "binder_driver", "hal", "dalvik")
# The dev app's intent extras (MainActivity) and RenderQuality preset names.
EXTRA_BENCHMARK = "benchmark"
EXTRA_SECONDS = "seconds"
EXTRA_QUALITY = "quality"
EXTRA_HUD = "hud"
EXTRA_POSE = "pose"
QUALITY_PRESETS = ("low", "medium", "high", "ultra")
DEFAULT_QUALITY = "high"
DEFAULT_SECONDS = 30
MS_PER_S = 1000
ENVIRONMENT = "physical"


def perfetto_config(duration_s):
    """Text-proto Perfetto config: scheduling, clocks, thermal, memory and the app's slices."""
    events = "\n".join(f'      ftrace_events: "{event}"' for event in FTRACE_EVENTS)
    categories = "\n".join(f'      atrace_categories: "{category}"' for category in ATRACE_CATEGORIES)
    return f"""buffers {{ size_kb: {TRACE_BUFFER_KB} fill_policy: RING_BUFFER }}
buffers {{ size_kb: {STATS_BUFFER_KB} fill_policy: RING_BUFFER }}
data_sources {{
  config {{
    name: "linux.ftrace"
    target_buffer: 0
    ftrace_config {{
{events}
{categories}
      atrace_apps: "{PACKAGE}"
    }}
  }}
}}
data_sources {{
  config {{
    name: "linux.process_stats"
    target_buffer: 1
    process_stats_config {{ scan_all_processes_on_start: true proc_stats_poll_ms: {PROCESS_POLL_MS} }}
  }}
}}
data_sources {{
  config {{
    name: "linux.sys_stats"
    target_buffer: 1
    sys_stats_config {{ meminfo_period_ms: {MEMINFO_PERIOD_MS} }}
  }}
}}
duration_ms: {int(duration_s * MS_PER_S)}
"""


def launch_args(seconds, quality, pose):
    """The am start command; --ez hud false keeps HUD redraws out of the measurement."""
    args = ["shell", "am", "start", "-n", ACTIVITY, "--ez", EXTRA_BENCHMARK, "true",
            "--ef", EXTRA_SECONDS, str(seconds), "--es", EXTRA_QUALITY, quality, "--ez", EXTRA_HUD, "false"]
    if pose:
        args += ["--es", EXTRA_POSE, pose]
    return args


def adb(serial, *args, check=True, **kwargs):
    command = ["adb"] + (["-s", serial] if serial else []) + list(args)
    return subprocess.run(command, check=check, capture_output=True, text=True, **kwargs)


def device_property(serial, name):
    return adb(serial, "shell", "getprop", name).stdout.strip()


def git_state():
    sha = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    dirty = subprocess.run(["git", "status", "--porcelain"], cwd=ROOT, capture_output=True, text=True).stdout
    return {"commit": sha, "dirty": bool(dirty.strip())}


def wait_for_benchmark(serial, timeout_s):
    """Polls logcat until the benchmark prints its GPU summary, or the world fails."""
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        log = adb(serial, "logcat", "-d", "-v", "threadtime").stdout
        if WORLD_FAILED in log:
            raise RuntimeError("the world failed to load; see logcat.txt")
        if BENCHMARK_DONE in log:
            return
        time.sleep(POLL_S)
    raise RuntimeError(f"no benchmark summary after {timeout_s:.0f} s")


def wait_for_exit(serial, pid, timeout_s):
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        # kill -0 is denied across SELinux domains; /proc shows whether it still runs.
        if adb(serial, "shell", "test", "-d", f"/proc/{pid}", check=False).returncode != 0:
            return
        time.sleep(POLL_S)
    raise RuntimeError("Perfetto did not finish writing the trace")


def run(args):
    if not args.skip_build:
        print(f"building the {BUILD_VARIANT} dev app", flush=True)
        subprocess.run(["./gradlew", GRADLE_TASK, "-q"], cwd=APP_DIR, check=True)
    print("installing", flush=True)
    adb(args.serial, "install", "-r", str(APK))

    out = args.out or RUNS_DIR / datetime.datetime.now().strftime("%Y-%m-%dT%H-%M-%S")
    out.mkdir(parents=True, exist_ok=True)
    adb(args.serial, "shell", "am", "force-stop", PACKAGE)
    adb(args.serial, "logcat", "-c")
    adb(args.serial, "shell", "rm", "-f", DEVICE_TRACE, check=False)

    trace_seconds = args.seconds + LOAD_ALLOWANCE_S
    print(f"tracing {trace_seconds:.0f} s: load, then a {args.seconds:g} s turn", flush=True)
    started = adb(args.serial, "shell", "perfetto", "--txt", "-c", "-", "-o", DEVICE_TRACE, "--background",
                  input=perfetto_config(trace_seconds))
    perfetto_pid = started.stdout.strip().splitlines()[-1]
    adb(args.serial, *launch_args(args.seconds, args.quality, args.pose))

    failure = None
    try:
        wait_for_benchmark(args.serial, trace_seconds)
    except RuntimeError as error:
        failure = str(error)
    app_pid = adb(args.serial, "shell", "pidof", PACKAGE, check=False).stdout.strip()
    log = adb(args.serial, "logcat", "-d", "-v", "threadtime").stdout
    (out / "logcat.txt").write_text(log)

    wait_for_exit(args.serial, perfetto_pid, trace_seconds + LOAD_ALLOWANCE_S)
    trace = out / "trace.perfetto-trace"
    adb(args.serial, "pull", DEVICE_TRACE, str(trace))
    adb(args.serial, "shell", "rm", "-f", DEVICE_TRACE, check=False)

    environment = {
        "device": device_property(args.serial, "ro.product.model"),
        "android": device_property(args.serial, "ro.build.version.release"),
        "soc": device_property(args.serial, "ro.board.platform"),
        "build": BUILD_VARIANT, "quality": args.quality, "pose": args.pose, "seconds": args.seconds,
        **git_state(),
    }
    summary = (benchmark_report.summarize(log, ENVIRONMENT, int(app_pid), args.seconds)
               if app_pid.isdigit() else {"status": "failed", "runs": [], "errors": ["app not running"]})
    summary["run"] = environment
    summary["trace"] = {"path": str(trace), "app_slices_found": FRAME_SLICE in trace.read_bytes()}
    if failure:
        summary["status"] = "failed"
        summary.setdefault("errors", []).append(failure)
    (out / "report.json").write_text(json.dumps(summary, indent=2, allow_nan=False))
    print_summary(summary, out)
    analyze(trace, out, app_pid)
    return 0 if summary["status"] == "passed" else 1


def analyze(trace, out, app_pid):
    """Runs perfetto_analysis.py on the trace in the perf venv; the recording stands without it."""
    if not PERF_PYTHON.exists():
        print(f"skipped trace analysis; set it up once with: {PERF_SETUP}")
        return
    print("analyzing the trace", flush=True)
    pid = ["--pid", app_pid] if app_pid.isdigit() else []
    subprocess.run([str(PERF_PYTHON), str(ANALYSIS), str(trace), "--json", str(out / "analysis.json"), *pid],
                   cwd=ROOT, check=False)


def print_summary(summary, out):
    for run_summary in summary.get("runs", []):
        if run_summary.get("status") != "complete":
            continue
        gpu = run_summary.get("gpu_mean_ms")
        stages = run_summary["stage_sample_mean_ms"]
        print(f"{run_summary['fps_mean']:.1f} fps, frame p95 {run_summary['frame_p95_ms']:.1f} ms, "
              f"gpu {'n/a' if gpu is None else f'{gpu:.1f}'} ms, "
              f"sort {stages['sort'] or 0:.1f} ms, cull {stages['cull'] or 0:.1f} ms")
    print(f"status {summary['status']}; app slices in trace: {summary['trace']['app_slices_found']}")
    for error in summary.get("errors", []):
        print(f"error: {error}")
    print(f"run directory: {out}\nopen {out / 'trace.perfetto-trace'} at https://ui.perfetto.dev")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--seconds", type=float, default=DEFAULT_SECONDS, help="length of the benchmark turn")
    parser.add_argument("--quality", default=DEFAULT_QUALITY, choices=QUALITY_PRESETS)
    parser.add_argument("--pose", help='"x,y,z,yaw,pitch"; the turn keeps the position, resets yaw')
    parser.add_argument("--serial", help="adb device serial when more than one is attached")
    parser.add_argument("--out", type=Path, help=f"run directory; defaults under {RUNS_DIR.relative_to(ROOT)}")
    parser.add_argument("--skip-build", action="store_true", help=f"install the last {BUILD_VARIANT} APK")
    args = parser.parse_args()
    try:
        return run(args)
    except (subprocess.CalledProcessError, RuntimeError, OSError) as error:
        detail = getattr(error, "stderr", None) or ""
        print(f"android_perf failed: {error}\n{detail}".rstrip(), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
