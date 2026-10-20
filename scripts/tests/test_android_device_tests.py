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


runner = load("android_device_tests")


class TestListTest(unittest.TestCase):
    def test_list_is_every_executable_the_target_links(self):
        executables = re.findall(r"add_executable\((splatkit_vulkan_\w+_test)\b", runner.TESTS_CMAKE.read_text())
        self.assertEqual(sorted(runner.test_names()), sorted(executables))
        self.assertGreater(len(executables), 0)

    def test_a_cmake_file_without_the_target_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            cmake = Path(directory) / "CMakeLists.txt"
            cmake.write_text("add_executable(splatkit_vulkan_buffer_test a.cpp)\n")
            with self.assertRaises(RuntimeError):
                runner.test_names(cmake)

    def test_a_target_that_links_no_test_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            cmake = Path(directory) / "CMakeLists.txt"
            cmake.write_text("add_custom_target(splatkit_android_tests DEPENDS splatkit_core)\n")
            with self.assertRaises(RuntimeError):
                runner.test_names(cmake)


class OutcomeTest(unittest.TestCase):
    def test_exit_zero_passes(self):
        self.assertEqual(runner.outcome(f"PASS buffer\n{runner.EXIT_MARKER}0\n"), (runner.Outcome.PASSED, 0))

    def test_exit_77_is_a_skip(self):
        self.assertEqual(runner.outcome(f"SKIP radix\n{runner.EXIT_MARKER}77\n"), (runner.Outcome.SKIPPED, 77))

    def test_nonzero_exit_fails(self):
        self.assertEqual(runner.outcome(f"FAIL radix: x\n{runner.EXIT_MARKER}1\n"), (runner.Outcome.FAILED, 1))

    def test_a_fail_line_fails_even_on_exit_zero(self):
        self.assertEqual(runner.outcome(f"FAIL case 3\n{runner.EXIT_MARKER}0\n"), (runner.Outcome.FAILED, 0))

    def test_a_lost_marker_fails(self):
        self.assertEqual(runner.outcome("PASS buffer\n"), (runner.Outcome.FAILED, None))

    def test_marker_survives_carriage_returns(self):
        self.assertEqual(runner.outcome(f"PASS\r\n{runner.EXIT_MARKER}0\r\n"), (runner.Outcome.PASSED, 0))


class CommandTest(unittest.TestCase):
    def test_runs_from_the_device_dir_and_echoes_the_status(self):
        line = runner.run_command("splatkit_vulkan_buffer_test", {})
        self.assertEqual(line, f"cd {runner.DEVICE_DIR} && ./splatkit_vulkan_buffer_test; "
                               f"echo {runner.EXIT_MARKER}$?")

    def test_environment_goes_before_the_binary(self):
        line = runner.run_command("splatkit_vulkan_radix_sort_test", {"SPLATKIT_RADIX_LARGE": "1"})
        self.assertIn("&& SPLATKIT_RADIX_LARGE=1 ./splatkit_vulkan_radix_sort_test;", line)

    def test_configure_builds_the_tests_for_the_floor(self):
        command = runner.configure_command(Path("/ndk"))
        self.assertIn("-DSPLATKIT_ANDROID_BUILD_TESTS=ON", command)
        self.assertIn(f"-DANDROID_PLATFORM={runner.PLATFORM}", command)
        self.assertIn("-DCMAKE_TOOLCHAIN_FILE=/ndk/build/cmake/android.toolchain.cmake", command)


if __name__ == "__main__":
    unittest.main()
