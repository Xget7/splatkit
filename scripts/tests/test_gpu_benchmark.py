import copy
import hashlib
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / "gpu_benchmark.py"
spec = importlib.util.spec_from_file_location("gpu_benchmark", SCRIPT)
suite = importlib.util.module_from_spec(spec)
spec.loader.exec_module(suite)


class ExperimentTest(unittest.TestCase):
    def setUp(self):
        self.config = json.loads(suite.DEFAULT.read_text())

    def test_both_backends_share_scenario_and_phone_time_budget(self):
        metal = suite.plan(self.config, "metal", "timing")
        vulkan = suite.plan(self.config, "vulkan", "timing")
        self.assertEqual(metal["scenario"], vulkan["scenario"])
        self.assertEqual(metal["active_seconds"], 140)
        self.assertEqual(
            [r["variant"] for r in metal["runs"] if r["phase"] == "timing"],
            ["before", "change", "change", "before"],
        )
        self.assertEqual(len(metal["runs"]), 6)
        self.assertEqual(
            [r["install"] for r in metal["runs"]],
            [True, False, True, False, False, True],
        )

    def test_diagnostics_do_not_silently_multiply_repeats(self):
        trace = suite.plan(self.config, "metal", "timeline")
        self.assertEqual(trace["active_seconds"], 60)
        self.assertEqual(len(trace["runs"]), 2)
        with self.assertRaisesRegex(ValueError, "Android energy"):
            suite.plan(self.config, "vulkan", "power")

    def test_invalid_experiments_fail_before_hardware_work(self):
        invalid = [
            ("seconds", float("nan")),
            ("budget", True),
            ("subpixel", -1),
            ("pose", [0, 0, 0, 1, 0]),
            ("order", ["before", "change"]),
            ("source_splats", 500000),
        ]
        for key, value in invalid:
            with self.subTest(key=key), self.assertRaises(ValueError):
                config = copy.deepcopy(self.config)
                config["scenario"][key] = value
                suite.plan(config, "metal", "timing")

    def fixture(self, root):
        plan = suite.plan(self.config, "metal", "timing")
        plan["device"] = "explicit-test-device"
        for name, build in plan["builds"].items():
            app = root / (name + ".app")
            app.mkdir()
            (app / "SplatKitDev").write_bytes(name.encode())
            build.update(
                artifact=str(app), commit=("a" if name == "before" else "b") * 40
            )
        return plan

    @staticmethod
    def write_report(experiment, item, artifact, out, nodes=13375264, status="passed"):
        out.mkdir()
        report = {
            "status": status,
            "run": {"validation": False},
            "runs": [
                {
                    "resident_records": nodes,
                    "fps_mean": 40 if item["variant"] == "before" else 44,
                    "frame_p95_ms": 30,
                    "gpu_mean_ms": 25,
                }
            ],
        }
        (out / "report.json").write_text(json.dumps(report))
        return 0

    def test_prepared_builds_and_reports_keep_the_correct_variant(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            experiment = self.fixture(root)
            with (
                patch.object(
                    suite, "execute_run", side_effect=self.write_report
                ) as execute,
                patch.object(suite, "plot"),
            ):
                suite.run(experiment, root / "out")
            self.assertEqual(execute.call_count, 6)
            report = json.loads((root / "out/02-change/report.json").read_text())
            self.assertEqual(report["installed_build"]["commit"], "b" * 40)
            self.assertEqual(
                report["installed_build"]["sha256"],
                hashlib.sha256(b"change").hexdigest(),
            )
            result = json.loads((root / "out/comparison.json").read_text())
            self.assertEqual(result["metrics"]["fps_mean"]["before"], [40, 40])
            self.assertAlmostEqual(result["metrics"]["fps_mean"]["delta_percent"], 10)
            with (
                self.assertRaises(FileExistsError),
                patch.object(suite, "execute_run") as second,
            ):
                suite.run(experiment, root / "out")
            second.assert_not_called()

    def test_wrong_world_stops_all_remaining_phone_runs_and_preserves_failure(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            experiment = self.fixture(root)

            def wrong(*args):
                return self.write_report(*args, nodes=500000)

            with (
                patch.object(suite, "execute_run", side_effect=wrong) as execute,
                self.assertRaisesRegex(RuntimeError, "run failed"),
            ):
                suite.run(experiment, root / "out")
            self.assertEqual(execute.call_count, 1)
            manifest = json.loads((root / "out/experiment.json").read_text())
            self.assertEqual(manifest["status"], "failed")
            self.assertTrue((root / "out/warmup-before/report.json").exists())
            with self.assertRaisesRegex(ValueError, "complete timing"):
                suite.compare(root / "out/experiment.json")

    def test_missing_second_build_never_touches_the_phone(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            experiment = self.fixture(root)
            Path(experiment["builds"]["change"]["artifact"], "SplatKitDev").unlink()
            with (
                patch.object(suite, "execute_run") as execute,
                self.assertRaisesRegex(ValueError, "missing"),
            ):
                suite.run(experiment, root / "out")
            execute.assert_not_called()
            self.assertFalse((root / "out").exists())

    def test_frame_gpu_measurements_stay_unavailable_when_absent(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            experiment = self.fixture(root)

            def absent(*args):
                self.write_report(*args)
                path = args[-1] / "report.json"
                r = json.loads(path.read_text())
                r["runs"][0]["gpu_mean_ms"] = None
                path.write_text(json.dumps(r))
                return 0

            with (
                patch.object(suite, "execute_run", side_effect=absent),
                patch.object(suite, "plot"),
            ):
                suite.run(experiment, root / "out")
            result = json.loads((root / "out/comparison.json").read_text())
            self.assertEqual(
                result["metrics"]["gpu_mean_ms"], {"status": "unavailable"}
            )

    def test_native_trace_experiment_is_not_ranked_as_a_timing_result(self):
        with tempfile.TemporaryDirectory() as scratch:
            path = Path(scratch) / "experiment.json"
            path.write_text(json.dumps(suite.plan(self.config, "metal", "timeline")))
            self.assertIsNone(suite.compare(path))

    def test_platform_adapters_keep_diagnostics_out_of_timing_and_reuse_installs(self):
        import android_perf
        import ios_perf

        item = {"phase": "timing", "seconds": 30, "install": False}
        artifact = Path("/prepared/app")
        for target, module in [("metal", ios_perf), ("vulkan", android_perf)]:
            experiment = suite.plan(self.config, target, "timing")
            calls = []

            def capture(args, calls=calls, target=target, module=module):
                calls.append(args)
                if target == "metal":
                    self.assertEqual(module.APP, artifact)
                else:
                    self.assertEqual(module.apk("release"), artifact)
                return 0

            with patch.object(module, "run", side_effect=capture):
                self.assertEqual(
                    suite.execute_run(experiment, item, artifact, Path("/output")), 0
                )
            self.assertTrue(calls[0].skip_install)
            self.assertFalse(calls[0].validation)
            if target == "metal":
                self.assertFalse(calls[0].trace)
                self.assertFalse(calls[0].power)
                self.assertNotEqual(module.APP, artifact)
            else:
                self.assertFalse(calls[0].gpu_stages)
                self.assertNotEqual(module.apk("release"), artifact)

    def test_markdown_exposes_repeat_ranges_and_unknown_sort_without_images(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            experiment = self.fixture(root)
            with patch.object(suite, "execute_run", side_effect=self.write_report):
                suite.run(experiment, root / "out")
            text = (root / "out/comparison.md").read_text()
            self.assertIn("40.00 (40.00-40.00)", text)
            self.assertIn("+10.0%", text)
            self.assertIn("Visibility + sort (sampled ms)", text)
            self.assertIn("Unmeasured | Unmeasured", text)
            self.assertNotIn("![", text)
            self.assertNotIn("None", text)
            self.assertFalse((root / "out/comparison.png").exists())

    def test_fps_gain_does_not_hide_a_sort_stage_slowdown(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            experiment = self.fixture(root)

            def stages(*args):
                self.write_report(*args)
                item, out = args[1], args[-1]
                path = out / "report.json"
                report = json.loads(path.read_text())
                report["runs"][0]["stage_valid_sample_mean_ms"] = {
                    "sort": 2 if item["variant"] == "before" else 4
                }
                path.write_text(json.dumps(report))
                return 0

            with patch.object(suite, "execute_run", side_effect=stages):
                suite.run(experiment, root / "out")
            result = json.loads((root / "out/comparison.json").read_text())
            self.assertEqual(result["optimization_decision"], "not_accepted")
            self.assertIn("Sort-stage slowdown", result["stage_warning"])
