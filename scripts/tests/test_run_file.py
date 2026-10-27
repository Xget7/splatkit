import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SCRIPTS = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


run_file = load("run_file")
CASES = json.loads(run_file.SAMPLES.read_text())


class SchemaTest(unittest.TestCase):
    def test_every_shared_case_meets_or_breaks_the_schema_at_its_key(self):
        for case in CASES:
            with self.subTest(case["name"]):
                if "error" in case:
                    with self.assertRaises(run_file.RunFileError) as raised:
                        run_file.validate(case["run"])
                    self.assertEqual(raised.exception.key, case["error"])
                else:
                    run_file.validate(case["run"])

    def test_a_case_only_one_platform_refuses_meets_the_schema(self):
        for case in CASES:
            if "error" not in case and ("ios_error" in case or "android_error" in case):
                with self.subTest(case["name"]):
                    run_file.validate(case["run"])

    def test_the_schema_uses_only_keywords_the_validator_checks(self):
        run_file.check_keywords(run_file.load_schema())

    def test_an_unchecked_keyword_is_refused(self):
        with self.assertRaises(ValueError):
            run_file.check_keywords({"type": "object", "dependentRequired": {}})


class DocumentTest(unittest.TestCase):
    def test_a_benchmark_holds_only_what_was_given(self):
        doc = run_file.document(30, "high")
        self.assertEqual(doc, {"schema_version": 1, "renderer": {"quality": "high"}, "run": {"seconds": 30}})

    def test_every_script_option_lands_in_its_section(self):
        doc = run_file.document(20, "high", pose="-7.25,1.6,5.25,0,0", world="scene.lodsplat",
                                source_frame="rdf", budget=1800000, lod_splat_limit=0, subpixel=1.0)
        self.assertEqual(doc["world"], {"file": "scene.lodsplat", "source_frame": "rdf"})
        self.assertEqual(doc["renderer"], {"quality": "high", "budget": 1800000, "lod_splat_limit": 0, "subpixel": 1.0})
        self.assertEqual(doc["pose"], [-7.25, 1.6, 5.25, 0, 0])
        run_file.validate(doc)

    def test_a_source_frame_needs_a_world(self):
        with self.assertRaises(ValueError):
            run_file.document(20, "high", source_frame="rdf")

    def test_a_pose_needs_five_numbers(self):
        with self.assertRaises(ValueError):
            run_file.document(20, "high", pose="1,2,3")

    def test_an_invalid_document_is_never_built(self):
        with self.assertRaises(run_file.RunFileError) as raised:
            run_file.document(0, "high")
        self.assertEqual(raised.exception.key, "run.seconds")

    def test_a_saved_run_file_reads_back_as_the_document(self):
        doc = run_file.document(30, "high", world="scene.lodsplat")
        with tempfile.TemporaryDirectory() as directory:
            run_file.save(doc, directory)
            self.assertEqual(json.loads((Path(directory) / run_file.SAVED_NAME).read_text()), doc)

    def test_encoding_is_one_compact_line(self):
        self.assertEqual(run_file.encode(run_file.document(5, "fast")),
                         '{"schema_version":1,"renderer":{"quality":"fast"},"run":{"seconds":5}}')


if __name__ == "__main__":
    unittest.main()
