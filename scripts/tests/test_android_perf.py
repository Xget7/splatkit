import importlib.util
from pathlib import Path
import re
import tempfile
import unittest

SCRIPTS = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


perf = load("android_perf")
analysis = load("perfetto_analysis")
MS = 1_000_000


class RecordingTest(unittest.TestCase):
    def test_config_traces_the_app_for_the_requested_time(self):
        config = perf.perfetto_config(45)
        self.assertIn(f'atrace_apps: "{perf.PACKAGE}"', config)
        self.assertIn("duration_ms: 45000", config)
        self.assertIn('ftrace_events: "sched/sched_switch"', config)

    def test_launch_runs_the_benchmark_without_the_hud(self):
        args = perf.launch_args(20, "high", None)
        self.assertEqual(args[args.index(perf.EXTRA_SECONDS) + 1], "20")
        self.assertEqual(args[args.index(perf.EXTRA_HUD) + 1], "false")
        self.assertNotIn(perf.EXTRA_POSE, args)
        self.assertEqual(perf.launch_args(20, "low", "1,2,3,0,0")[-1], "1,2,3,0,0")


class SceneAssetsTest(unittest.TestCase):
    def test_missing_asset_is_rejected_with_its_path_and_the_fix(self):
        with tempfile.TemporaryDirectory() as directory:
            assets = Path(directory)
            (assets / perf.SCENE_ASSETS[0]).write_bytes(b"")
            missing = assets / perf.SCENE_ASSETS[1]
            with self.assertRaises(RuntimeError) as raised:
                perf.require_scene_assets(assets)
            message = str(raised.exception)
            self.assertIn(str(missing), message)
            self.assertNotIn(str(assets / perf.SCENE_ASSETS[0]), message)
            self.assertIn("git-ignored", message)
            self.assertNotIn("\n", message)

    def test_every_missing_asset_is_named(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(RuntimeError) as raised:
                perf.require_scene_assets(Path(directory))
            for name in perf.SCENE_ASSETS:
                self.assertIn(name, str(raised.exception))

    def test_present_assets_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            for name in perf.SCENE_ASSETS:
                (Path(directory) / name).write_bytes(b"")
            perf.require_scene_assets(Path(directory))

    def test_scene_is_the_one_the_dev_app_opens_from_its_assets(self):
        activity = perf.APP_DIR / "app/src/main/java/com/splatkit/devapp/MainActivity.kt"
        opened = tuple(re.findall(r'assets\.open\("([^"]+)"\)', activity.read_text()))
        self.assertEqual(perf.SCENE_ASSETS, opened)


class AnalysisTest(unittest.TestCase):
    def test_nearest_rank_percentile(self):
        self.assertEqual(analysis.percentile([4, 1, 3, 2], 50), 2)
        self.assertEqual(analysis.percentile([4, 1, 3, 2], 95), 4)
        self.assertIsNone(analysis.percentile([], 50))

    def test_distribution_reports_milliseconds(self):
        result = analysis.distribution_ms([2 * MS, 4 * MS])
        self.assertEqual(result["count"], 2)
        self.assertEqual(result["mean_ms"], 3)
        self.assertEqual(result["max_ms"], 4)
        self.assertEqual(analysis.distribution_ms([]), {"count": 0})

    def test_clusters_group_cpus_by_maximum_frequency(self):
        clusters = analysis.clusters_by_max_frequency(
            {0: 1785600, 1: 1785600, 4: 2419200, 5: 2419200, 7: 2841600})
        self.assertEqual([c["cpus"] for c in clusters], [[0, 1], [4, 5], [7]])
        self.assertEqual(clusters[-1]["max_mhz"], 2841.6)

    def test_pacing_counts_intervals_past_the_hitch_factor(self):
        starts = [0, 16 * MS, 32 * MS, 66 * MS, 82 * MS]
        result = analysis.pacing(starts)
        self.assertEqual(result["count"], 4)
        self.assertEqual(result["hitches"], 1)

    def test_breakdown_splits_running_by_cluster_and_keeps_waits(self):
        clusters = [{"cpus": [0, 1]}, {"cpus": [4]}]
        states = [{"state": analysis.RUNNING, "cpu": 0, "dur": 10}, {"state": analysis.RUNNING, "cpu": 4, "dur": 30},
                  {"state": "S", "cpu": None, "dur": 60}]
        result = analysis.render_thread_breakdown(states, clusters, (0, 100))
        self.assertEqual([c["pct"] for c in result["running_pct_by_cluster"]], [10, 30])
        self.assertEqual(result["running_pct"], 40)
        self.assertEqual(result["state_pct"], {"S": 60})


if __name__ == "__main__":
    unittest.main()
