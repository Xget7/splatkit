import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "export-ios-source.py"
spec = importlib.util.spec_from_file_location("export_source", SCRIPT)
export = importlib.util.module_from_spec(spec)
spec.loader.exec_module(export)


class PublishedPathTest(unittest.TestCase):
    def test_native_platforms_keep_repository_paths(self):
        for platform in ("ios", "android"):
            name = "packages/react-native-splatkit/package.json"
            self.assertEqual(export.published(platform, name), name)

    def test_ios_distribution_files_move_to_the_repository_root(self):
        self.assertEqual(export.published("ios", "packages/splatkit-ios/distribution/README.md"), "README.md")
        name = "packages/splatkit-ios/distribution/README.md"
        self.assertEqual(export.published("android", name), name)

    def test_react_native_package_becomes_the_repository_root(self):
        self.assertEqual(
            export.published("react-native", "packages/react-native-splatkit/src/index.ts"),
            "src/index.ts")
        self.assertEqual(
            export.published("react-native",
                             "packages/react-native-splatkit/distribution/.github/workflows/contract.yml"),
            ".github/workflows/contract.yml")

    def test_react_native_keeps_root_files(self):
        self.assertEqual(export.published("react-native", "LICENSE"), "LICENSE")

    def test_only_a_leading_distribution_directory_is_unwrapped(self):
        self.assertEqual(
            export.published("react-native", "packages/react-native-splatkit/src/distribution/a.ts"),
            "src/distribution/a.ts")


class ExportedWorkflowTest(unittest.TestCase):
    def test_native_core_workflow_inputs_are_exported(self):
        for platform in ("ios", "android"):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as scratch:
                result = subprocess.check_output(
                    [sys.executable, str(SCRIPT), "--platform", platform],
                    env={**os.environ, "TMPDIR": scratch}, text=True)
                directory = Path(json.loads(result)["directory"])
                manifest = json.loads((directory / "source-manifest.json").read_text())
                workflow = (directory / ".github/workflows/core.yml").read_text()
                # Check the paths the exported job actually consumes, including npm's
                # package manifest beside its cache lockfile. No node_modules may leak.
                inputs = set(re.findall(r"cache-dependency-path:\s+(scripts/[\w./-]+)", workflow))
                inputs.update(re.findall(r"\bnode\s+(scripts/[\w./-]+)", workflow))
                for prefix in re.findall(r"\bnpm ci --prefix\s+(scripts/[\w./-]+)", workflow):
                    inputs.update((f"{prefix}/package.json", f"{prefix}/package-lock.json"))
                self.assertTrue(inputs, "the interop job must exercise exported inputs")
                for name in sorted(inputs):
                    self.assertTrue(name in manifest, f"{platform} workflow dependency missing: {name}")
                    self.assertEqual((directory / name).read_bytes(), (export.ROOT / name).read_bytes())
                self.assertFalse(any("node_modules" in Path(name).parts for name in manifest))


@unittest.skipUnless((export.ROOT / ".github/workflows/mirror.yml").is_file(),
                     "mirror workflow belongs to the upstream repository")
class MirrorCopyTest(unittest.TestCase):
    def test_changed_bytes_are_copied_even_with_equal_size_and_timestamp(self):
        workflow = (export.ROOT / ".github/workflows/mirror.yml").read_text()
        command = next(line.strip() for line in workflow.splitlines() if line.strip().startswith("rsync "))
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            source, mirror = root / "source", root / "mirror"
            source.mkdir()
            mirror.mkdir()
            # Export and clone can write equal-sized manifests during the same second.
            # rsync's default quick check then leaves the previous hash behind.
            for directory, value in ((source, "new"), (mirror, "old")):
                path = directory / "source-manifest.json"
                path.write_text(json.dumps({"file": value}) + "\n")
                os.utime(path, (1_600_000_000, 1_600_000_000))
            args = [arg.replace("${DIRECTORY}", str(source)) for arg in shlex.split(command)]
            subprocess.run(args, cwd=root, check=True, capture_output=True)
            self.assertEqual((mirror / "source-manifest.json").read_bytes(),
                             (source / "source-manifest.json").read_bytes())


if __name__ == "__main__":
    unittest.main()
