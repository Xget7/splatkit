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


class WorldAndValidationTest(unittest.TestCase):
    def test_world_goes_to_the_extra_the_dev_app_reads(self):
        args = perf.launch_args(20, "high", None, "house.spz")
        self.assertEqual(args[args.index(perf.EXTRA_WORLD) + 1], "house.spz")
        self.assertNotIn(perf.EXTRA_WORLD, perf.launch_args(20, "high", None))
        activity = perf.APP_DIR / "app/src/main/java/com/splatkit/devapp/MainActivity.kt"
        self.assertIn(f'getStringExtra("{perf.EXTRA_WORLD}")', activity.read_text())

    def test_lod_controls_go_to_the_extras_the_dev_app_reads(self):
        args = perf.launch_args(20, "high", None, budget=300000, lod_splat_limit=150000)
        self.assertEqual(args[args.index(perf.EXTRA_BUDGET) - 1], "--ei")
        self.assertEqual(args[args.index(perf.EXTRA_BUDGET) + 1], "300000")
        self.assertEqual(args[args.index(perf.EXTRA_LOD_SPLAT_LIMIT) + 1], "150000")
        self.assertNotIn(perf.EXTRA_BUDGET, perf.launch_args(20, "high", None))
        self.assertNotIn(perf.EXTRA_LOD_SPLAT_LIMIT, perf.launch_args(20, "high", None))
        activity = (perf.APP_DIR / "app/src/main/java/com/splatkit/devapp/MainActivity.kt").read_text()
        self.assertIn(f'getIntExtra("{perf.EXTRA_BUDGET}"', activity)
        self.assertIn(f'getIntExtra("{perf.EXTRA_LOD_SPLAT_LIMIT}"', activity)

    def test_the_cutoff_goes_to_the_extra_the_dev_app_reads(self):
        args = perf.launch_args(20, "high", None, subpixel=1.0)
        self.assertEqual(args[args.index(perf.EXTRA_SUBPIXEL) - 1], "--ef")
        self.assertEqual(args[args.index(perf.EXTRA_SUBPIXEL) + 1], "1.0")
        self.assertNotIn(perf.EXTRA_SUBPIXEL, perf.launch_args(20, "high", None))
        activity = (perf.APP_DIR / "app/src/main/java/com/splatkit/devapp/MainActivity.kt").read_text()
        self.assertIn(f'getFloatExtra("{perf.EXTRA_SUBPIXEL}"', activity)

    def test_the_source_frame_goes_to_the_extra_the_dev_app_reads(self):
        args = perf.launch_args(20, "high", None, "kitchen.spz", source_frame="rdf")
        self.assertEqual(args[args.index(perf.EXTRA_SOURCE_FRAME) - 1], "--es")
        self.assertEqual(args[args.index(perf.EXTRA_SOURCE_FRAME) + 1], "rdf")
        self.assertNotIn(perf.EXTRA_SOURCE_FRAME, perf.launch_args(20, "high", None, "kitchen.spz"))
        activity = (perf.APP_DIR / "app/src/main/java/com/splatkit/devapp/MainActivity.kt").read_text()
        self.assertIn(f'getStringExtra("{perf.EXTRA_SOURCE_FRAME}"', activity)

    def test_relative_worlds_live_in_the_external_files_dir(self):
        self.assertEqual(perf.device_world("house.spz"), f"/sdcard/Android/data/{perf.PACKAGE}/files/house.spz")
        self.assertEqual(perf.device_world("/sdcard/house.spz"), "/sdcard/house.spz")

    def test_validation_builds_and_installs_the_debug_apk(self):
        self.assertEqual(perf.gradle_task(perf.VALIDATION_VARIANT), ":app:assembleDebug")
        self.assertEqual(perf.gradle_task(perf.BUILD_VARIANT), ":app:assembleRelease")
        self.assertEqual(perf.apk("debug").name, "app-debug.apk")

    def test_a_validation_run_needs_the_layer_on(self):
        self.assertIsNone(perf.validation_failure(f"x SplatKit: {perf.VALIDATION_ON}\n"))
        self.assertIn("did not load", perf.validation_failure("x SplatKit: validation layers: off\n"))

    def test_the_layer_status_line_is_what_vulkan_context_logs(self):
        context = (perf.ROOT / "packages/splatkit-android/src/main/cpp/rendering/vulkan/device/VulkanContext.cpp")
        self.assertIn('LOGI("validation layers: %s", validation ? "on" : "off")', context.read_text())

    def test_a_missing_layer_names_the_fetch_script(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(RuntimeError) as raised:
                perf.require_validation_layer(Path(directory) / "libVkLayer_khronos_validation.so")
            self.assertIn("fetch-validation-layers.sh", str(raised.exception))


class CapabilitiesTest(unittest.TestCase):
    LOG = ("10-20 20:57:18.1 1 2 I SplatKit: Vulkan device: Adreno (TM) 640, API 1.1.128, driver 0x801f6000\n"
           "10-20 20:57:18.1 1 2 I SplatKit: Vulkan capabilities: subgroup 64, compute subgroup ops basic vote "
           "arithmetic ballot shuffle shuffle_relative, shared memory 32768 B, workgroup invocations 1024, "
           "storage buffer range 536870912 B, device-local heap 5767012352 B, float16 arithmetic yes, "
           "16-bit storage no\n")

    def test_the_gpu_block_holds_the_reported_limits(self):
        self.assertEqual(perf.gpu_capabilities(self.LOG), {
            "device": "Adreno (TM) 640", "api": "1.1.128", "driver": "0x801f6000",
            "subgroup_size": 64,
            "compute_subgroup_ops": ["basic", "vote", "arithmetic", "ballot", "shuffle", "shuffle_relative"],
            "shared_memory_bytes": 32768, "workgroup_invocations": 1024,
            "storage_buffer_range_bytes": 536870912, "device_local_heap_bytes": 5767012352,
            "float16_arithmetic": True, "storage_16bit": False,
        })

    def test_no_compute_subgroups_is_an_empty_list(self):
        log = self.LOG.replace("basic vote arithmetic ballot shuffle shuffle_relative", "none")
        self.assertEqual(perf.gpu_capabilities(log)["compute_subgroup_ops"], [])

    def test_a_log_without_the_lines_has_no_gpu_block(self):
        self.assertIsNone(perf.gpu_capabilities("SplatKit: benchmark started\n"))

    def test_the_pattern_follows_what_vulkan_context_logs(self):
        context = perf.ROOT / "packages/splatkit-android/src/main/cpp/rendering/vulkan/device/VulkanContext.cpp"
        source = context.read_text()
        for fragment in ('"Vulkan capabilities: %s"', '"subgroup "', '", compute subgroup ops "',
                         '", shared memory "', '" B, workgroup invocations "', '", storage buffer range "',
                         '" B, device-local heap "', '" B, float16 arithmetic "', '", 16-bit storage "',
                         '"Vulkan device: %s, API %u.%u.%u, driver 0x%x"'):
            self.assertIn(fragment, source)


class RotationTest(unittest.TestCase):
    def setUp(self):
        self.settings = {perf.AUTO_ROTATE: "1", perf.USER_ROTATION: "null"}
        self.original = (perf.system_setting, perf.put_system_setting)
        perf.system_setting = lambda serial, name: self.settings[name]
        perf.put_system_setting = lambda serial, name, value: self.settings.__setitem__(name, value)

    def tearDown(self):
        perf.system_setting, perf.put_system_setting = self.original

    def test_a_run_locks_landscape_and_restores_the_settings(self):
        saved = perf.lock_rotation(None)
        self.assertEqual(self.settings, {perf.AUTO_ROTATE: "0", perf.USER_ROTATION: perf.RUN_ROTATION})
        perf.restore_rotation(None, saved)
        self.assertEqual(self.settings[perf.AUTO_ROTATE], "1")
        # Unset before the run, so nothing is written back; the lock's value stays harmless
        # because auto-rotate is on again.
        self.assertEqual(self.settings[perf.USER_ROTATION], perf.RUN_ROTATION)


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
        source = activity.read_text()
        constants = dict(re.findall(r'const val (\w+) = "([^"]+)"', source))
        opened = tuple(constants[name] for name in re.findall(r"assets\.open\((\w+)\)", source))
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

class NativeGpuTraceTest(unittest.TestCase):
    def test_vendor_sources_are_used_exactly_without_guessing_counter_support(self):
        query = 'name: "gpu.renderstages.adreno" name: "gpu.renderstages" name: "gpu.counters"'
        sources = perf.gpu_stage_sources(query)
        self.assertEqual(sources, ['gpu.renderstages','gpu.renderstages.adreno'])
        config = perf.perfetto_config(30, sources)
        self.assertIn('name: "gpu.renderstages.adreno"',config)
        self.assertNotIn('name: "gpu.counters"',config)
        self.assertEqual(perf.gpu_stage_sources('name: "linux.ftrace"'), [])
        self.assertNotIn('gpu.renderstages',perf.perfetto_config(30))

    def test_gpu_query_aggregates_native_slices_inside_the_window(self):
        import sqlite3
        db=sqlite3.connect(':memory:'); db.row_factory=sqlite3.Row
        db.executescript('''
            create table gpu_track(id integer, name text);
            create table gpu_slice(name text, track_id integer, ts integer, dur integer);
            insert into gpu_track values(1,'Compute');
            insert into gpu_slice values('visibility',1,1000000,1000000);
            insert into gpu_slice values('visibility',1,2000000,2000000);
            insert into gpu_slice values('past-window',1,9000000,3000000);
            insert into gpu_slice values('unfinished',1,3000000,-1);
        ''')
        trace=analysis.Trace.__new__(analysis.Trace)
        trace.rows=lambda sql:[dict(row) for row in db.execute(sql)]
        try:
            result=trace.gpu_stages((1000000,10000000))
            self.assertEqual(result['status'],'measured')
            self.assertEqual(result['stages'],[{'name':'visibility','queue':'Compute','samples':2,'mean_ms':1.5,'max_ms':2.0}])
            self.assertIn('device-wide',result['scope'])
            self.assertEqual(trace.gpu_stages((20000000,30000000))['status'],'unavailable')
        finally:
            db.close()

    def test_unsupported_schema_does_not_become_zero_gpu_time(self):
        trace=analysis.Trace.__new__(analysis.Trace)
        def missing(sql): raise RuntimeError('no such table: gpu_slice')
        trace.rows=missing
        result=trace.gpu_stages((0,100))
        self.assertEqual(result['status'],'unavailable')
        self.assertIn('gpu_slice',result['reason'])
        self.assertNotIn('mean_ms',result)
