import importlib.util
from pathlib import Path
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


if __name__ == "__main__":
    unittest.main()
