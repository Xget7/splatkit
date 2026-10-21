#!/usr/bin/env python3
"""Build the Android dev app as release, run its benchmark turn under Perfetto and summarize it.

Writes trace.perfetto-trace, logcat.txt and report.json to one run directory; open the trace
at https://ui.perfetto.dev. Needs one adb device; the phone must stay awake and unlocked.
--world names a file in the app's external files dir; --validation runs the debug build with the
Khronos validation layer and fails on any message it logs.
"""

import argparse
import datetime
import json
from pathlib import Path
import re
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
import benchmark_report  # noqa: E402
import perfetto_analysis  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
APP_DIR = ROOT / "apps" / "android-dev"
BUILD_VARIANT = "release"
# Only the debug build looks for the validation layer (VulkanContext wantValidation).
VALIDATION_VARIANT = "debug"
VALIDATION_LAYER = APP_DIR / "app" / "src" / "debug" / "jniLibs" / "arm64-v8a" / "libVkLayer_khronos_validation.so"
VALIDATION_ON = "validation layers: on"
ASSETS_DIR = APP_DIR / "app" / "src" / "main" / "assets"
# The scene MainActivity loads from its assets when the launch carries no world extra.
SCENE_ASSETS = ("kitchen_500k.spz", "kitchen_collider.glb")
PACKAGE = perfetto_analysis.PACKAGE
# MainActivity resolves a relative world path against getExternalFilesDir(null).
DEVICE_FILES_DIR = f"/sdcard/Android/data/{PACKAGE}/files"
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
EXTRA_WORLD = "world"
EXTRA_BUDGET = "budget"
EXTRA_LOD_SPLAT_LIMIT = "lodSplatLimit"
QUALITY_PRESETS = ("low", "medium", "high", "ultra")
# Every run draws in landscape, rotation 1 (Surface.ROTATION_90), whatever way the phone lies;
# the previous settings come back afterwards.
AUTO_ROTATE = "accelerometer_rotation"
USER_ROTATION = "user_rotation"
RUN_ROTATION = "1"
ROTATION_OFF = "0"
DEFAULT_QUALITY = "high"
DEFAULT_SECONDS = 30
MS_PER_S = 1000
# VulkanContext logs this line once per engine.
DEVICE = re.compile(r"Vulkan device: (?P<device>.+?), API (?P<api>\d+\.\d+\.\d+), driver (?P<driver>0x[0-9a-f]+)")
CAPABILITIES = re.compile(r"Vulkan capabilities: subgroup (?P<subgroup_size>\d+), "
                          r"compute subgroup ops (?P<compute_subgroup_ops>[a-z_ ]+), "
                          r"shared memory (?P<shared_memory_bytes>\d+) B, "
                          r"workgroup invocations (?P<workgroup_invocations>\d+), "
                          r"storage buffer range (?P<storage_buffer_range_bytes>\d+) B, "
                          r"device-local heap (?P<device_local_heap_bytes>\d+) B, "
                          r"float16 arithmetic (?P<float16_arithmetic>yes|no), "
                          r"16-bit storage (?P<storage_16bit>yes|no)")
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


def run_settings(args):
    """The turn's settings as the report records them."""
    return {"quality": args.quality, "pose": args.pose, "world": args.world, "budget": args.budget,
            "lod_splat_limit": args.lod_splat_limit, "seconds": args.seconds, "validation": args.validation}


def launch_args(seconds, quality, pose, world=None, budget=None, lod_splat_limit=None):
    """The am start command; --ez hud false keeps HUD redraws out of the measurement."""
    args = ["shell", "am", "start", "-n", ACTIVITY, "--ez", EXTRA_BENCHMARK, "true",
            "--ef", EXTRA_SECONDS, str(seconds), "--es", EXTRA_QUALITY, quality, "--ez", EXTRA_HUD, "false"]
    if pose:
        args += ["--es", EXTRA_POSE, pose]
    if world:
        args += ["--es", EXTRA_WORLD, world]
    if budget is not None:
        args += ["--ei", EXTRA_BUDGET, str(budget)]
    if lod_splat_limit is not None:
        args += ["--ei", EXTRA_LOD_SPLAT_LIMIT, str(lod_splat_limit)]
    return args


def apk(variant):
    return APP_DIR / "app" / "build" / "outputs" / "apk" / variant / f"app-{variant}.apk"


def gradle_task(variant):
    return f":app:assemble{variant.capitalize()}"


def device_world(world):
    return world if world.startswith("/") else f"{DEVICE_FILES_DIR}/{world}"


def require_validation_layer(layer=VALIDATION_LAYER):
    if not layer.is_file():
        raise RuntimeError(f"missing {layer}: fetch it with scripts/fetch-validation-layers.sh")


def validation_failure(log):
    """Why a validation run does not count, or None; the messages themselves are report errors."""
    if VALIDATION_ON not in log:
        return "the validation layer did not load; see logcat.txt"
    return None


def gpu_capabilities(log):
    """report.json's gpu block, from the device and capabilities lines; None without them."""
    device, capabilities = DEVICE.search(log), CAPABILITIES.search(log)
    if device is None or capabilities is None:
        return None
    gpu = dict(device.groupdict())
    for key, value in capabilities.groupdict().items():
        if key == "compute_subgroup_ops":
            gpu[key] = [] if value == "none" else value.split()
        elif value in ("yes", "no"):
            gpu[key] = value == "yes"
        else:
            gpu[key] = int(value)
    return gpu


def adb(serial, *args, check=True, **kwargs):
    command = ["adb"] + (["-s", serial] if serial else []) + list(args)
    return subprocess.run(command, check=check, capture_output=True, text=True, **kwargs)


def device_property(serial, name):
    return adb(serial, "shell", "getprop", name).stdout.strip()


def git_state():
    sha = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    dirty = subprocess.run(["git", "status", "--porcelain"], cwd=ROOT, capture_output=True, text=True).stdout
    return {"commit": sha, "dirty": bool(dirty.strip())}


def system_setting(serial, name):
    return adb(serial, "shell", "settings", "get", "system", name).stdout.strip()


def put_system_setting(serial, name, value):
    adb(serial, "shell", "settings", "put", "system", name, value)


def lock_rotation(serial):
    """Locks the display to RUN_ROTATION; returns the settings that restore_rotation puts back."""
    saved = {name: system_setting(serial, name) for name in (AUTO_ROTATE, USER_ROTATION)}
    put_system_setting(serial, AUTO_ROTATE, ROTATION_OFF)
    put_system_setting(serial, USER_ROTATION, RUN_ROTATION)
    return saved


def restore_rotation(serial, saved):
    for name, value in saved.items():
        # An unset setting reads back as "null" and is left unset.
        if value != "null":
            put_system_setting(serial, name, value)


def require_scene_assets(assets_dir=ASSETS_DIR):
    """Fails before the build when a bundled scene file is absent; the build would not notice."""
    missing = [assets_dir / name for name in SCENE_ASSETS if not (assets_dir / name).is_file()]
    if missing:
        paths = ", ".join(str(path) for path in missing)
        raise RuntimeError(f"missing scene file {paths}: the scene files are git-ignored, "
                           "copy them in from a checkout that has them")


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
    variant = VALIDATION_VARIANT if args.validation else BUILD_VARIANT
    if args.validation:
        require_validation_layer()
    if not args.skip_build:
        if not args.world:
            require_scene_assets()
        print(f"building the {variant} dev app", flush=True)
        subprocess.run(["./gradlew", gradle_task(variant), "-q"], cwd=APP_DIR, check=True)
    if args.world and adb(args.serial, "shell", "test", "-f", device_world(args.world), check=False).returncode != 0:
        raise RuntimeError(f"no world at {device_world(args.world)}; push it there with adb push")
    print("installing", flush=True)
    adb(args.serial, "install", "-r", str(apk(variant)))

    saved_rotation = lock_rotation(args.serial)
    try:
        return record(args, variant)
    finally:
        restore_rotation(args.serial, saved_rotation)


def record(args, variant):
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
    adb(args.serial, *launch_args(args.seconds, args.quality, args.pose, args.world, args.budget,
                                   args.lod_splat_limit))

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
        "build": variant, **run_settings(args), "rotation": int(RUN_ROTATION),
        **git_state(),
    }
    summary = (benchmark_report.summarize(log, ENVIRONMENT, int(app_pid), args.seconds)
               if app_pid.isdigit() else {"status": "failed", "runs": [], "errors": ["app not running"]})
    summary["run"] = environment
    summary["gpu"] = gpu_capabilities(log)
    summary["trace"] = {"path": str(trace), "app_slices_found": FRAME_SLICE in trace.read_bytes()}
    failure = failure or (validation_failure(log) if args.validation else None)
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
    parser.add_argument("--world", help=f"world file, relative to {DEVICE_FILES_DIR} or absolute; "
                        "default the bundled kitchen_500k.spz")
    parser.add_argument("--budget", type=int,
                        help="LOD budget: a world loads as a hierarchy drawing at most this many splats")
    parser.add_argument("--lod-splat-limit", type=int,
                        help="most splats one frame of a hierarchy world selects, 0 for the budget")
    parser.add_argument("--validation", action="store_true",
                        help="debug build with the Khronos validation layer; the timings are then not comparable")
    parser.add_argument("--serial", help="adb device serial when more than one is attached")
    parser.add_argument("--out", type=Path, help=f"run directory; defaults under {RUNS_DIR.relative_to(ROOT)}")
    parser.add_argument("--skip-build", action="store_true", help="install the last APK of the build")
    args = parser.parse_args()
    try:
        return run(args)
    except (subprocess.CalledProcessError, RuntimeError, OSError) as error:
        detail = getattr(error, "stderr", None) or ""
        print(f"android_perf failed: {error}\n{detail}".rstrip(), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
