import importlib.util
from pathlib import Path
import tempfile
import subprocess
import unittest
from unittest.mock import patch
import zipfile

spec = importlib.util.spec_from_file_location(
    "check_android_alignment", Path(__file__).resolve().parents[1] / "check_android_alignment.py")
alignment = importlib.util.module_from_spec(spec)
spec.loader.exec_module(alignment)
GOOD_ELF = {"load_alignments": [16384], "relro_segments_checked": 1}


class AndroidAlignmentTest(unittest.TestCase):
    def test_all_load_segments_must_be_aligned(self):
        good = " LOAD 0x000000 0x000000 0x000000 0x001000 0x001000 R E 0x4000\n"
        self.assertEqual(alignment.check_load_segments(good), [16384])
        with self.assertRaisesRegex(ValueError, "not 16 KB"):
            alignment.check_load_segments(good + good.replace("0x4000", "0x1000"))

    def test_empty_and_noncongruent_loads_fail(self):
        with self.assertRaisesRegex(ValueError, "Malformed"):
            alignment.check_load_segments("LOAD 0x1000")
        with self.assertRaisesRegex(ValueError, "no LOAD"):
            alignment.check_load_segments("Program Headers:")
        with self.assertRaisesRegex(ValueError, "congruent"):
            alignment.check_load_segments("LOAD 0x1000 0x4000 0x4000 0x10 0x10 RW 0x4000")

    def test_relro_end_alignment_and_absence(self):
        self.assertEqual(alignment.check_relro_segments(""), 0)
        self.assertEqual(alignment.check_relro_segments(
            "GNU_RELRO 0x1000 0x5000 0x5000 0x1000 0x3000 R 0x1"), 1)
        with self.assertRaisesRegex(ValueError, "GNU_RELRO end"):
            alignment.check_relro_segments(
                "GNU_RELRO 0x1000 0x5000 0x5000 0x1000 0x1000 R 0x1")
        with self.assertRaisesRegex(ValueError, "Malformed"):
            alignment.check_relro_segments("GNU_RELRO")

    def test_archive_checks_every_library_and_apk_zip_alignment(self):
        with tempfile.TemporaryDirectory() as directory:
            apk = Path(directory) / "app.apk"
            with zipfile.ZipFile(apk, "w") as archive:
                archive.writestr("lib/arm64-v8a/libfirst.so", b"first")
                archive.writestr("lib/arm64-v8a/libsecond.so", b"second")
            with patch.object(alignment, "check_elf", return_value=GOOD_ELF) as elf, \
                    patch.object(alignment.subprocess, "run") as run:
                result = alignment.check_artifact(apk, "readelf", "zipalign")
                self.assertEqual(elf.call_count, 2)
                run.assert_called_once_with(
                    ["zipalign", "-c", "-P", "16", "-v", "4", str(apk)],
                    check=True, capture_output=True, text=True)
                self.assertTrue(result["apk_zip_alignment_checked"])
                self.assertFalse(result["runtime_16kb_validated"])
            with patch.object(alignment, "check_elf", return_value=GOOD_ELF), \
                    patch.object(alignment.subprocess, "run", side_effect=
                                 subprocess.CalledProcessError(1, "zipalign")):
                with self.assertRaises(subprocess.CalledProcessError):
                    alignment.check_artifact(apk, "readelf", "zipalign")
            with patch.object(alignment, "check_elf", side_effect=
                              [GOOD_ELF, ValueError("bad dependency")]):
                with self.assertRaisesRegex(ValueError, "bad dependency"):
                    alignment.check_artifact(apk, "readelf", "zipalign")
            with patch.object(alignment, "check_elf", return_value=GOOD_ELF):
                with self.assertRaisesRegex(ValueError, "require --zipalign"):
                    alignment.check_artifact(apk, "readelf")

    def test_archive_without_native_libraries_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            aar = Path(directory) / "empty.aar"
            with zipfile.ZipFile(aar, "w"):
                pass
            with self.assertRaisesRegex(ValueError, "no shared libraries"):
                alignment.check_artifact(aar, "readelf")


if __name__ == "__main__":
    unittest.main()
