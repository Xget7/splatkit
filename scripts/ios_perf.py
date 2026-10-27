#!/usr/bin/env python3
"""Build the iOS dev app as release, run its benchmark turn on an iPhone and summarize it.

Writes console.log and report.json, plus metal.trace with --trace or power.trace and exported
XML with --power. Needs a paired iPhone that stays awake and unlocked; --world names a file
in the app's Documents or bundle. Power comparisons require unplugged wireless debugging.
"""

import argparse
import datetime
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
import benchmark_report  # noqa: E402
import ios_power  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
APP_DIR = ROOT / "apps" / "ios-dev"
PROJECT = APP_DIR / "SplatKitDev.xcodeproj"
SCHEME = "SplatKitDev"
CONFIGURATION = "Release"
DERIVED_DATA = ROOT / "build" / "ios-dev-derived"
APP = DERIVED_DATA / "Build" / "Products" / f"{CONFIGURATION}-iphoneos" / "SplatKitDev.app"
BUNDLE_ID = "com.splatkit.devapp"
EXECUTABLE = "SplatKitDev.app/SplatKitDev"
RUNS_DIR = ROOT / "output" / "ios-perf"
# The dev app's QualityTier raw values.
QUALITY_PRESETS = ("ultra", "high", "balanced", "fast")
DEFAULT_QUALITY = "high"
DEFAULT_SECONDS = 30
# World decode and upload before the turn starts.
LOAD_ALLOWANCE_S = 60
POLL_S = 1.0
ENVIRONMENT = "physical"
VALIDATION_VARIABLES = {"MTL_DEBUG_LAYER": "1", "MTL_SHADER_VALIDATION": "1"}
# devicectl --console relays NSLog only with this set.
CONSOLE_VARIABLES = {"OS_ACTIVITY_DT_MODE": "enable"}
TRACE_TEMPLATE = "Metal System Trace"
POWER_TEMPLATE = "Power Profiler"
# Over Wi-Fi the iPhone 17 Pro took about 5 minutes to save a 30 s trace and as long for a 10 s one.
TRACE_SAVE_ALLOWANCE_S = 600
# A Wi-Fi Metal System Trace once stopped at its time limit and never saved, so it needs a cable.
# Power Profiler stays on Wi-Fi: charging over the cable would distort the power reading.
CABLE = "wired"

BENCHMARK_STARTED = "benchmark started:"
BENCHMARK_DONE = "benchmark gpu ms:"
FAILURE = re.compile(r"SplatKit E: |SplatWorldMissing: |SplatRunStopped: ")
# Engine errors, Metal API Validation assertions and Shader Validation reports.
ERROR = re.compile(r"SplatKit E: |failed assertion|-\[MTLDebug|Invalid (device|constant|threadgroup|texture) "
                   r"(load|store)|Shader Validation|SplatWorldMissing: ")
WAITING, STARTED, DONE, FAILED = "waiting", "started", "done", "failed"
# The turn holds pitch, so a change during it means a touch moved the camera.
PITCH = re.compile(r" pitch (-?\d+\.\d+),")
# MetalContext::capabilities(), logged once when the renderer starts.
CAPABILITIES = re.compile(r"Metal capabilities: family (?P<family>Apple\d+), Metal (?P<metal>\d+), "
                          r"threadgroup memory (?P<threadgroup_memory_bytes>\d+) B, "
                          r"threads per threadgroup (?P<threads_per_threadgroup>\d+), "
                          r"max buffer (?P<max_buffer_bytes>\d+) B, working set (?P<working_set_bytes>\d+) B")


def launch_command(udid, seconds, quality, pose, world, validation, budget=None, subpixel=None):
    """The devicectl launch that streams the app's console; --hud 0 keeps HUD redraws out."""
    variables = {**CONSOLE_VARIABLES, **(VALIDATION_VARIABLES if validation else {})}
    command = ["xcrun", "devicectl", "device", "process", "launch", "--device", udid, "--console",
               "--terminate-existing", "--environment-variables", json.dumps(variables), BUNDLE_ID,
               "--benchmark", str(seconds), "--quality", quality, "--hud", "0", "--keep-awake", "1"]
    if pose:
        command += ["--pose", pose]
    if world:
        command += ["--world", world]
    if budget is not None:
        command += ["--budget", str(budget)]
    if subpixel is not None:
        command += ["--subpixel", str(subpixel)]
    return command


def devicectl_json(*args):
    """Runs a devicectl query and returns its --json-output document."""
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "out.json"
        subprocess.run(["xcrun", "devicectl", *args, "--json-output", str(path)],
                       check=True, capture_output=True, text=True)
        return json.loads(path.read_text())


def device_environment(details):
    properties = details["result"]["deviceProperties"]
    hardware = details["result"]["hardwareProperties"]
    return {"device": hardware["marketingName"], "name": properties["name"], "model": hardware["productType"],
            "ios": properties["osVersionNumber"], "ios_build": properties["osBuildUpdate"],
            "transport": details["result"]["connectionProperties"]["transportType"]}


def unlocked(lock_state):
    return not lock_state["result"]["passcodeRequired"]


def app_pid(processes):
    for process in processes["result"]["runningProcesses"]:
        if process.get("executable", "").endswith(EXECUTABLE):
            return process["processIdentifier"]
    return None


def console_errors(log):
    return [line for line in log.splitlines() if ERROR.search(line)]


def gpu_capabilities(log):
    match = CAPABILITIES.search(log)
    if match is None:
        return None
    return {key: value if key == "family" else int(value) for key, value in match.groupdict().items()}


def touch_failure(log):
    turn = log.partition(BENCHMARK_STARTED)[2].partition(BENCHMARK_DONE)[0]
    if len(set(PITCH.findall(turn))) > 1:
        return "the camera pitch changed during the turn; the screen was touched"
    return None


def benchmark_state(log):
    if FAILURE.search(log):
        return FAILED
    if BENCHMARK_DONE in log:
        return DONE
    if BENCHMARK_STARTED in log:
        return STARTED
    return WAITING


def git_state():
    sha = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    dirty = subprocess.run(["git", "status", "--porcelain"], cwd=ROOT, capture_output=True, text=True).stdout
    return {"commit": sha, "dirty": bool(dirty.strip())}


def build(udid):
    print("building the iOS libraries and the release dev app", flush=True)
    subprocess.run([str(ROOT / "scripts" / "build-ios.sh"), CONFIGURATION], cwd=ROOT, check=True,
                   capture_output=True, text=True)
    subprocess.run(["xcodegen", "generate", "--quiet"], cwd=APP_DIR, check=True)
    subprocess.run(["xcodebuild", "-project", str(PROJECT), "-scheme", SCHEME, "-configuration", CONFIGURATION,
                    "-destination", f"id={udid}", "-derivedDataPath", str(DERIVED_DATA),
                    "-allowProvisioningUpdates", "-quiet", "build"], cwd=ROOT, check=True)


def trace_command(device_name, pid, seconds, path, template=TRACE_TEMPLATE):
    """Metal System Trace attached to the app for the turn's length.

    Names the device and the pid: xctrace lists a phone reached over Wi-Fi as offline under its UDID,
    and finds no process by the app's name there.
    """
    # The stock Power Profiler template also enables network/CPU/Metal instruments.
    # Instruments 27.1 traps in NetworkConnectionStatsModeling when closing that template.
    instruments = ["--template", "Blank", "--instrument", POWER_TEMPLATE] if template == POWER_TEMPLATE else ["--template", template]
    return ["xcrun", "xctrace", "record", *instruments, "--device", device_name,
            "--attach", str(pid), "--time-limit", f"{seconds:g}s", "--no-prompt", "--output", str(path)]


def trace_failure(requested, recorded, template=TRACE_TEMPLATE):
    if requested and not recorded:
        return f"{template} was not recorded; see trace.log"
    return None


def watch(console_path, timeout_s, on_started):
    """Polls the console log until the benchmark finishes or the run fails."""
    deadline = time.monotonic() + timeout_s
    started = False
    while time.monotonic() < deadline:
        state = benchmark_state(console_path.read_text(errors="replace"))
        if state in (DONE, FAILED):
            return state
        if state == STARTED and not started:
            started = True
            on_started()
        time.sleep(POLL_S)
    raise RuntimeError(f"no benchmark summary after {timeout_s:.0f} s")


def terminate_app(udid):
    pid = app_pid(devicectl_json("device", "info", "processes", "--device", udid))
    if pid is not None:
        subprocess.run(["xcrun", "devicectl", "device", "process", "terminate", "--device", udid,
                        "--pid", str(pid)], check=False, capture_output=True)


def run(args):
    if not unlocked(devicectl_json("device", "info", "lockState", "--device", args.udid)):
        raise RuntimeError("the iPhone is locked; unlock it and keep it awake")
    device = device_environment(devicectl_json("device", "info", "details", "--device", args.udid))
    if args.trace and device["transport"] != CABLE:
        raise RuntimeError(f"the iPhone is on {device['transport']}; connect it by USB to record a trace")
    if args.power and int(device["ios"].split(".")[0]) < 26:
        raise RuntimeError("Power Profiler requires iOS 26 or later")
    if not args.skip_build:
        build(args.udid)
    if not args.skip_install:
        print("installing", flush=True)
        subprocess.run(["xcrun", "devicectl", "device", "install", "app", "--device", args.udid, str(APP)],
                       check=True, capture_output=True, text=True)

    out = args.out or RUNS_DIR / datetime.datetime.now().strftime("%Y-%m-%dT%H-%M-%S")
    out.mkdir(parents=True, exist_ok=False)
    console_path = out / "console.log"
    template = POWER_TEMPLATE if args.power else TRACE_TEMPLATE
    tracing = args.trace or args.power
    trace_path = out / ("power.trace" if args.power else "metal.trace")
    trace = []
    print(f"running a {args.seconds:g} s turn", flush=True)
    with console_path.open("w") as console, (out / "trace.log").open("w") as trace_log:
        app = subprocess.Popen(launch_command(args.udid, args.seconds, args.quality, args.pose, args.world,
                                              args.validation, args.budget, args.subpixel),
                               stdout=console, stderr=subprocess.STDOUT)
        def on_started():
            if tracing:
                pid = app_pid(devicectl_json("device", "info", "processes", "--device", args.udid))
                if pid is None:
                    raise RuntimeError("the benchmark process disappeared before trace attachment")
                trace.append(subprocess.Popen(trace_command(device["name"], pid, args.seconds, trace_path, template),
                                              stdout=trace_log, stderr=subprocess.STDOUT))

        failure = None
        try:
            state = watch(console_path, args.seconds + LOAD_ALLOWANCE_S, on_started)
            if state == FAILED:
                failure = "the run failed; see console.log"
        except RuntimeError as error:
            failure = str(error)
        finally:
            # Let xctrace finish and close its document before terminating its target.
            # A recorder crash or target termination can leave a bundle without a template.
            # The completed benchmark leaves a still view; the engine draws only on changes.
            for recorder in trace:
                try:
                    recorder.wait(timeout=TRACE_SAVE_ALLOWANCE_S)
                except subprocess.TimeoutExpired:
                    recorder.kill()
                    recorder.wait()
            terminate_app(args.udid)
            app.terminate()
            app.wait(timeout=10)

    log = console_path.read_text(errors="replace")
    environment = {**device, "build": CONFIGURATION, "quality": args.quality, "pose": args.pose, "world": args.world,
                   "budget": args.budget, "subpixel": args.subpixel, "seconds": args.seconds,
                   "validation": args.validation, "reused_install": args.skip_install,
                   "installed_build_provenance": "not verified" if args.skip_install else str(APP), **git_state()}
    summary = benchmark_report.summarize(log, ENVIRONMENT, None, args.seconds)
    summary["run"] = environment
    summary["gpu"] = gpu_capabilities(log)
    summary["errors"] = sorted(set(summary["errors"]) | set(console_errors(log)))
    # A killed recorder can leave a partial trace bundle behind.
    recorded = bool(trace) and trace[0].returncode == 0 and trace_path.exists()
    if tracing:
        summary["trace"] = {"path": str(trace_path), "recorded": recorded, "template": template,
                            "recorder_exit_code": trace[0].returncode if trace else None}
    if args.power:
        summary["power"] = {
            "metrics_status": "unvalidated", "charging_state": "not verified",
            "coverage": "attachment requested after benchmark start; initial frames may be missing",
            "limitations": "Inspect charging, thermal state and time range in Instruments before comparing.",
        }
        if recorded:
            summary["power"]["export"] = ios_power.export_trace(trace_path, out / "power-export")
            summary["power"]["metrics_status"] = summary["power"]["export"].get("metrics_status", "unvalidated")
            if summary["power"]["export"]["status"] != "exported":
                failure = failure or "Power trace export failed; retry offline with scripts/ios_power.py"
    failure = failure or trace_failure(tracing, recorded, template) or touch_failure(log)
    if failure or summary["errors"]:
        summary["status"] = "failed"
    if failure:
        summary["errors"].append(failure)
    (out / "report.json").write_text(json.dumps(summary, indent=2, allow_nan=False))
    print_summary(summary, out)
    return 0 if summary["status"] == "passed" else 1


def print_summary(summary, out):
    for run_summary in summary.get("runs", []):
        if run_summary.get("status") != "complete":
            continue
        gpu = run_summary.get("gpu_mean_ms")
        stages = run_summary["stage_sample_mean_ms"]
        print(f"{run_summary['fps_mean']:.1f} fps, frame p95 {run_summary['frame_p95_ms']:.1f} ms, "
              f"gpu {'n/a' if gpu is None else f'{gpu:.1f}'} ms, sort {stages['sort'] or 0:.1f} ms, "
              f"select {stages['select'] or 0:.1f} ms, drawn {run_summary['drawn_sample_range']}")
    print(f"status {summary['status']}")
    if "power" in summary:
        print("power metrics unvalidated: inspect charging, thermal state and capture coverage in Instruments")
    for error in summary.get("errors", [])[:10]:
        print(f"error: {error}")
    print(f"run directory: {out}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--udid", required=True, help="the iPhone's UDID, from xcrun devicectl list devices")
    parser.add_argument("--seconds", type=float, default=DEFAULT_SECONDS, help="length of the benchmark turn")
    parser.add_argument("--quality", default=DEFAULT_QUALITY, choices=QUALITY_PRESETS)
    parser.add_argument("--pose", help='"x,y,z,yaw,pitch"; the turn keeps the position, resets yaw')
    parser.add_argument("--world", help="world file in the app's Documents or bundle; default kitchen_500k.spz")
    parser.add_argument("--budget", type=int,
                        help="LOD budget: the world loads as a hierarchy drawing at most this many splats")
    parser.add_argument("--subpixel", type=float, help="sub-pixel cutoff in pixels; the policy default otherwise")
    tracing = parser.add_mutually_exclusive_group()
    tracing.add_argument("--trace", action="store_true", help=f"also record {TRACE_TEMPLATE} for the turn")
    tracing.add_argument("--power", action="store_true", help="record and export Power Profiler; iOS 26+, unplugged Wi-Fi")
    parser.add_argument("--validation", action="store_true",
                        help="enable Metal API and Shader Validation; the timings are then not comparable")
    parser.add_argument("--out", type=Path, help=f"run directory; defaults under {RUNS_DIR.relative_to(ROOT)}")
    parser.add_argument("--skip-build", action="store_true", help="install the last release build")
    parser.add_argument("--skip-install", action="store_true", help="reuse the installed app; requires --skip-build")
    args = parser.parse_args()
    if args.skip_install and not args.skip_build:
        parser.error("--skip-install requires --skip-build")
    if args.power and args.validation:
        parser.error("--power cannot be combined with --validation")
    if not 0 < args.seconds <= 3600:
        parser.error("--seconds must be finite and in (0, 3600]")
    try:
        return run(args)
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired, RuntimeError, OSError) as error:
        detail = getattr(error, "stderr", None) or ""
        print(f"ios_perf failed: {error}\n{detail}".rstrip(), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
