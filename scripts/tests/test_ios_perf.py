import importlib.util
from pathlib import Path
import unittest
import tempfile
from types import SimpleNamespace
from unittest.mock import MagicMock, patch

SCRIPTS = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


perf = load("ios_perf")
UDID = "00008150-0019719C34E2401C"


class LaunchTest(unittest.TestCase):
    def test_launch_runs_the_benchmark_without_the_hud_or_validation(self):
        command = perf.launch_command(UDID, 20, "high", None, None, validation=False)
        app_args = command[command.index(perf.BUNDLE_ID) + 1:]
        self.assertEqual(app_args[app_args.index("--benchmark") + 1], "20")
        self.assertEqual(app_args[app_args.index("--hud") + 1], "0")
        self.assertEqual(app_args[app_args.index("--quality") + 1], "high")
        self.assertNotIn("--pose", app_args)
        self.assertNotIn("--world", app_args)
        self.assertNotIn("MTL_SHADER_VALIDATION", " ".join(command))
        self.assertLess(command.index("--console"), command.index(perf.BUNDLE_ID))

    def test_launch_passes_pose_world_and_validation(self):
        command = perf.launch_command(UDID, 30, "fast", "1,2,3,0,0", "lioness.spz", validation=True)
        app_args = command[command.index(perf.BUNDLE_ID) + 1:]
        self.assertEqual(app_args[app_args.index("--pose") + 1], "1,2,3,0,0")
        self.assertEqual(app_args[app_args.index("--world") + 1], "lioness.spz")
        environment = command[command.index("--environment-variables") + 1]
        self.assertIn('"MTL_SHADER_VALIDATION": "1"', environment)
        self.assertIn('"MTL_DEBUG_LAYER": "1"', environment)


    def test_budget_and_cutoff_go_to_the_switches_the_dev_app_reads(self):
        command = perf.launch_command(UDID, 30, "high", None, None, validation=False, budget=300000,
                                      subpixel=1.0)
        app_args = command[command.index(perf.BUNDLE_ID) + 1:]
        self.assertEqual(app_args[app_args.index("--budget") + 1], "300000")
        self.assertEqual(app_args[app_args.index("--subpixel") + 1], "1.0")
        plain = perf.launch_command(UDID, 30, "high", None, None, validation=False)
        self.assertNotIn("--budget", plain)
        self.assertNotIn("--subpixel", plain)
        app = perf.ROOT / "apps/ios-dev/SplatKitDev"
        self.assertIn('args.int("budget")', (app / "ContentView.swift").read_text())
        self.assertIn('args.float("subpixel")', (app / "SplatViewHost.swift").read_text())

class DeviceTest(unittest.TestCase):
    def test_environment_reads_model_os_and_family(self):
        details = {"result": {"deviceProperties": {"name": "xgetphone", "osVersionNumber": "27.0",
                                                    "osBuildUpdate": "24A437"},
                              "hardwareProperties": {"marketingName": "iPhone 17 Pro", "productType": "iPhone18,1"}}}
        self.assertEqual(perf.device_environment(details),
                         {"device": "iPhone 17 Pro", "name": "xgetphone", "model": "iPhone18,1", "ios": "27.0",
                          "ios_build": "24A437"})

    def test_trace_targets_the_device_by_name_and_the_app_by_pid(self):
        # xctrace lists a phone reached over Wi-Fi as offline under its UDID and online under its name.
        command = perf.trace_command("xgetphone", 812, 30, Path("/runs/1/metal.trace"))
        self.assertEqual(command[command.index("--device") + 1], "xgetphone")
        self.assertEqual(command[command.index("--attach") + 1], "812")
        self.assertEqual(command[command.index("--time-limit") + 1], "30s")
        self.assertEqual(command[command.index("--output") + 1], "/runs/1/metal.trace")

    def test_unlocked_needs_no_passcode(self):
        self.assertTrue(perf.unlocked({"result": {"passcodeRequired": False}}))
        self.assertFalse(perf.unlocked({"result": {"passcodeRequired": True}}))

    def test_app_pid_finds_the_dev_app_process(self):
        processes = {"result": {"runningProcesses": [
            {"executable": "file:///sbin/launchd", "processIdentifier": 1},
            {"executable": "file:///private/var/containers/Bundle/Application/X/SplatKitDev.app/SplatKitDev",
             "processIdentifier": 812}]}}
        self.assertEqual(perf.app_pid(processes), 812)
        self.assertIsNone(perf.app_pid({"result": {"runningProcesses": []}}))


class CapabilitiesTest(unittest.TestCase):
    def test_capabilities_come_from_the_startup_line(self):
        log = ("2026-10-20 SplatKitDev[1:2] SplatKit I: Apple A19 Pro GPU, Metal\n"
               "2026-10-20 SplatKitDev[1:2] SplatKit I: Metal capabilities: family Apple10, Metal 4, "
               "threadgroup memory 32768 B, threads per threadgroup 1024, max buffer 9663676416 B, "
               "working set 9663676416 B\n")
        self.assertEqual(perf.gpu_capabilities(log),
                         {"family": "Apple10", "metal": 4, "threadgroup_memory_bytes": 32768,
                          "threads_per_threadgroup": 1024, "max_buffer_bytes": 9663676416,
                          "working_set_bytes": 9663676416})
        self.assertIsNone(perf.gpu_capabilities("SplatKit I: Apple A19 Pro GPU, Metal"))


class TraceTest(unittest.TestCase):
    def test_power_reuses_install_and_saves_trace_before_terminating_attached_app(self):
        events = []
        recorder = MagicMock(returncode=0)
        recorder.poll.return_value = None
        recorder.wait.side_effect = lambda **kwargs: events.append("save trace")
        app = MagicMock()
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / "run"
            args = SimpleNamespace(udid=UDID, power=True, trace=False, skip_build=True, skip_install=True,
                                   out=out, seconds=30, quality="high", pose=None, world=None,
                                   validation=False, budget=None, subpixel=None)

            def popen(command, **kwargs):
                if "xctrace" in command:
                    (out / "power.trace").mkdir()
                    return recorder
                return app

            def watch(path, timeout, on_started):
                on_started()
                return perf.DONE

            def export(trace, destination):
                events.append("offline export")
                return {"status": "exported"}

            with patch.object(perf, "devicectl_json", return_value={}), \
                    patch.object(perf, "unlocked", return_value=True), \
                    patch.object(perf, "device_environment", return_value={"ios": "27.0", "name": "phone"}), \
                    patch.object(perf, "app_pid", return_value=812), \
                    patch.object(perf, "build") as build, \
                    patch.object(perf.subprocess, "run") as command, \
                    patch.object(perf.subprocess, "Popen", side_effect=popen), \
                    patch.object(perf, "watch", side_effect=watch), \
                    patch.object(perf, "terminate_app", side_effect=lambda udid: events.append("stop app")), \
                    patch.object(perf, "git_state", return_value={}), \
                    patch.object(perf.benchmark_report, "summarize", return_value={"status": "passed", "errors": []}), \
                    patch.object(perf.ios_power, "export_trace", side_effect=export), \
                    patch.object(perf, "print_summary"), patch("builtins.print"):
                self.assertEqual(perf.run(args), 0)
                build.assert_not_called()
                command.assert_not_called()
            self.assertEqual(events, ["save trace", "stop app", "offline export"])
            report = (out / "report.json").read_text()
            self.assertIn('"metrics_status": "unvalidated"', report)
            self.assertIn('"recorded": true', report)

    def test_power_attaches_to_one_process_and_keeps_fractional_duration(self):
        command = perf.trace_command("phone", 812, 0.5, Path("power.trace"), perf.POWER_TEMPLATE)
        self.assertEqual(command[command.index("--template") + 1], "Blank")
        self.assertEqual(command[command.index("--instrument") + 1], "Power Profiler")
        self.assertEqual(command[command.index("--attach") + 1], "812")
        self.assertEqual(command[command.index("--time-limit") + 1], "0.5s")
        self.assertNotIn("--all-processes", command)

    def test_invalid_combinations_fail_before_contacting_a_device(self):
        for arguments in (["--power", "--trace"], ["--power", "--validation"], ["--skip-install"],
                          ["--seconds", "nan"], ["--seconds", "0"], ["--seconds", "3601"]):
            with patch("sys.argv", ["ios_perf.py", "--udid", UDID, *arguments]), \
                    patch.object(perf, "run") as run, patch("sys.stderr"):
                with self.assertRaises(SystemExit) as error:
                    perf.main()
                self.assertEqual(error.exception.code, 2)
                run.assert_not_called()

    def test_a_requested_trace_that_was_not_recorded_fails_the_run(self):
        self.assertIsNone(perf.trace_failure(requested=False, recorded=False))
        self.assertIsNone(perf.trace_failure(requested=True, recorded=True))
        self.assertIn("trace.log", perf.trace_failure(requested=True, recorded=False))


class ConsoleTest(unittest.TestCase):
    def test_engine_errors_and_validation_reports_count_as_errors(self):
        log = "\n".join([
            "2026-10-20 SplatKitDev[1:2] SplatKit I: 60.0 fps, gpu 5.0 ms",
            "2026-10-20 SplatKitDev[1:2] SplatKit E: render command failed: Internal Error",
            "-[MTLDebugComputeCommandEncoder setBuffer:offset:atIndex:]:1234: failed assertion `index out of range'",
            "Invalid device load executing kernel function \"cullSplats\"",
        ])
        self.assertEqual(len(perf.console_errors(log)), 3)
        self.assertEqual(perf.console_errors("SplatKit I: benchmark gpu ms: mean 5.0"), [])

    def test_a_pitch_change_during_the_turn_means_the_screen_was_touched(self):
        def stats(pitch):
            return f"SplatKit I: 120.0 fps, gpu 4.9 ms, pos 0.00 0.00 0.00, yaw 1.00 pitch {pitch}, fly"
        steady = "\n".join([stats("0.30"), "SplatKit I: benchmark started: 10 splats", stats("0.00"),
                            stats("0.00"), "SplatKit I: benchmark gpu ms: mean 4.9", stats("0.40")])
        self.assertIsNone(perf.touch_failure(steady))
        touched = steady.replace(stats("0.00"), stats("-0.24"), 1)
        self.assertIn("touched", perf.touch_failure(touched))

    def test_state_follows_benchmark_and_world_failure(self):
        self.assertEqual(perf.benchmark_state("SplatKit I: benchmark started: 10 splats, one turn over 30.000 s"),
                         perf.STARTED)
        self.assertEqual(perf.benchmark_state("benchmark started: 1\nbenchmark gpu ms: mean 1 p50 1 p95 1 max 1"),
                         perf.DONE)
        self.assertEqual(perf.benchmark_state("SplatKit E: load failed: no such file"), perf.FAILED)
        self.assertEqual(perf.benchmark_state("SplatWorldMissing: lioness.spz"), perf.FAILED)
        self.assertEqual(perf.benchmark_state("SplatRunStopped: reason=thermal"), perf.FAILED)
        self.assertEqual(perf.benchmark_state(""), perf.WAITING)


if __name__ == "__main__":
    unittest.main()
