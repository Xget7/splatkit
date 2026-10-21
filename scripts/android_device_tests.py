#!/usr/bin/env python3
"""Build the Vulkan device tests, push them to one adb device, run each one and report.

The test list is the splatkit_android_tests target in packages/splatkit-android/tests/CMakeLists.txt.
A test passes on exit 0, is skipped on exit 77 (the device lacks what it needs) and fails otherwise.
Writes one log per test and report.json to a run directory.
"""

import argparse
import datetime
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
TESTS_CMAKE = ROOT / "packages" / "splatkit-android" / "tests" / "CMakeLists.txt"
CPP_DIR = ROOT / "packages" / "splatkit-android" / "src" / "main" / "cpp"
BUILD_DIR = ROOT / "build" / "android-tests"
BINARY_DIR = BUILD_DIR / "tests"
TARGET = "splatkit_android_tests"
DEVICE_DIR = "/data/local/tmp/splatkit-tests"
RUNS_DIR = ROOT / "output" / "android-device-tests"
# The NDK the AAR builds with; its ndkVersion line is the one place it is pinned for Gradle.
GRADLE_BUILD = ROOT / "packages" / "splatkit-android" / "build.gradle.kts"
NDK_VERSION_LINE = re.compile(r'ndkVersion = "([^"]+)"')
ABI = "arm64-v8a"
PLATFORM = "android-29"
EXIT_PASSED = 0
EXIT_SKIPPED = 77
EXIT_MARKER = "splatkit-test-exit="
# User builds keep the Vulkan loader from loading layers for shell executables;
# validation runs in the debug dev app (android_perf.py --validation).
DEBUGGABLE = "ro.debuggable"
TARGET_BLOCK = re.compile(rf"add_custom_target\({TARGET} DEPENDS(.*?)\)", re.S)
TEST_NAME = re.compile(r"\bsplatkit_vulkan_\w+_test\b")
EXIT_LINE = re.compile(rf"^{EXIT_MARKER}(\d+)\s*$", re.M)


class Outcome:
    PASSED = "passed"
    SKIPPED = "skipped"
    FAILED = "failed"


def test_names(cmake=TESTS_CMAKE):
    block = TARGET_BLOCK.search(cmake.read_text())
    if not block:
        raise RuntimeError(f"no {TARGET} target in {cmake}")
    names = TEST_NAME.findall(block.group(1))
    if not names:
        raise RuntimeError(f"{TARGET} in {cmake} links no test; a run of none would pass")
    return names


def ndk_version(gradle=GRADLE_BUILD):
    match = NDK_VERSION_LINE.search(gradle.read_text())
    if not match:
        raise RuntimeError(f"no ndkVersion in {gradle}")
    return match.group(1)


def ndk_dir():
    version = ndk_version()
    sdk = os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT") or str(Path.home() / "Library/Android/sdk")
    ndk = Path(sdk) / "ndk" / version
    if not ndk.is_dir():
        raise RuntimeError(f"NDK {version} not found at {ndk}; install it with sdkmanager \"ndk;{version}\"")
    return ndk


def configure_command(ndk):
    return ["cmake", "-S", str(CPP_DIR), "-B", str(BUILD_DIR),
            f"-DCMAKE_TOOLCHAIN_FILE={ndk / 'build/cmake/android.toolchain.cmake'}",
            f"-DANDROID_ABI={ABI}", f"-DANDROID_PLATFORM={PLATFORM}", "-DANDROID_STL=c++_static",
            "-DCMAKE_BUILD_TYPE=Release", "-DSPLATKIT_ANDROID_BUILD_TESTS=ON"]


def run_command(name, env):
    """The device shell line; the marker carries the exit status past adb shells that drop it."""
    assignments = " ".join(f"{key}={shlex.quote(value)}" for key, value in sorted(env.items()))
    prefix = f"{assignments} " if assignments else ""
    return f"cd {DEVICE_DIR} && {prefix}./{name}; echo {EXIT_MARKER}$?"


def outcome(output):
    """(outcome, exit status) from a test's output; a missing marker means adb lost the run."""
    found = EXIT_LINE.findall(output)
    if not found:
        return Outcome.FAILED, None
    status = int(found[-1])
    if status == EXIT_PASSED and "FAIL" not in output:
        return Outcome.PASSED, status
    if status == EXIT_SKIPPED:
        return Outcome.SKIPPED, status
    return Outcome.FAILED, status


def adb(serial, *args, check=True):
    command = ["adb"] + (["-s", serial] if serial else []) + list(args)
    return subprocess.run(command, check=check, capture_output=True, text=True)


def device_property(serial, name):
    return adb(serial, "shell", "getprop", name).stdout.strip()


def git_state():
    sha = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    dirty = subprocess.run(["git", "status", "--porcelain"], cwd=ROOT, capture_output=True, text=True).stdout
    return {"commit": sha, "dirty": bool(dirty.strip())}


def build():
    if not (BUILD_DIR / "CMakeCache.txt").exists():
        print(f"configuring {BUILD_DIR.relative_to(ROOT)}", flush=True)
        subprocess.run(configure_command(ndk_dir()), check=True, capture_output=True, text=True)
    print(f"building {TARGET}", flush=True)
    subprocess.run(["cmake", "--build", str(BUILD_DIR), "--target", TARGET, "--parallel"],
                   check=True, capture_output=True, text=True)


def run(args):
    names = test_names()
    if args.only:
        unknown = sorted(set(args.only) - set(names))
        if unknown:
            raise RuntimeError(f"unknown test {', '.join(unknown)}; the tests are {', '.join(names)}")
        names = [name for name in names if name in args.only]
    if not args.skip_build:
        build()
    missing = [name for name in names if not (BINARY_DIR / name).is_file()]
    if missing:
        raise RuntimeError(f"not built: {', '.join(missing)}; run without --skip-build")

    out = args.out or RUNS_DIR / datetime.datetime.now().strftime("%Y-%m-%dT%H-%M-%S")
    out.mkdir(parents=True, exist_ok=True)
    adb(args.serial, "shell", "mkdir", "-p", DEVICE_DIR)
    env = {"SPLATKIT_RADIX_LARGE": "1"} if args.large else {}
    results = []
    for name in names:
        adb(args.serial, "push", str(BINARY_DIR / name), f"{DEVICE_DIR}/{name}")
        adb(args.serial, "shell", "chmod", "755", f"{DEVICE_DIR}/{name}")
        print(f"running {name}", flush=True)
        ran = adb(args.serial, "shell", run_command(name, env), check=False)
        output = ran.stdout + ran.stderr
        (out / f"{name}.txt").write_text(output)
        result, status = outcome(output)
        results.append({"name": name, "outcome": result, "exit": status})
        print(f"  {result}" + ("" if status is None else f" (exit {status})"), flush=True)

    debuggable = device_property(args.serial, DEBUGGABLE) == "1"
    report = {
        "status": Outcome.FAILED if any(r["outcome"] == Outcome.FAILED for r in results) else Outcome.PASSED,
        "tests": results,
        "validation": ("the build is debuggable; layers for shell executables are untested here" if debuggable
                       else "unavailable: a user build loads no layers for shell executables; "
                            "run android_perf.py --validation"),
        "run": {
            "device": device_property(args.serial, "ro.product.model"),
            "android": device_property(args.serial, "ro.build.version.release"),
            "soc": device_property(args.serial, "ro.board.platform"),
            "large": args.large,
            **git_state(),
        },
    }
    (out / "report.json").write_text(json.dumps(report, indent=2))
    counts = {kind: sum(r["outcome"] == kind for r in results)
              for kind in (Outcome.PASSED, Outcome.SKIPPED, Outcome.FAILED)}
    print(f"{counts[Outcome.PASSED]} passed, {counts[Outcome.SKIPPED]} skipped, {counts[Outcome.FAILED]} failed")
    print(f"validation: {report['validation']}")
    print(f"run directory: {out}")
    return 0 if report["status"] == Outcome.PASSED else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--serial", help="adb device serial when more than one is attached")
    parser.add_argument("--only", nargs="+", metavar="TEST", help="run these tests only")
    parser.add_argument("--large", action="store_true", help="add the radix sort's 3M-key case")
    parser.add_argument("--skip-build", action="store_true", help=f"push the binaries already in {BINARY_DIR.relative_to(ROOT)}")
    parser.add_argument("--out", type=Path, help=f"run directory; defaults under {RUNS_DIR.relative_to(ROOT)}")
    args = parser.parse_args()
    try:
        return run(args)
    except (subprocess.CalledProcessError, RuntimeError, OSError) as error:
        detail = getattr(error, "stderr", None) or ""
        print(f"android_device_tests failed: {error}\n{detail}".rstrip(), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
