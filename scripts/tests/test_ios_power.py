import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location("ios_power", Path(__file__).resolve().parents[1] / "ios_power.py")
power = importlib.util.module_from_spec(spec)
spec.loader.exec_module(power)


class ExportTest(unittest.TestCase):
    def test_power_schema_without_samples_is_unavailable_not_zero_consumption(self):
        def export(command, **kwargs):
            path = Path(command[command.index("--output") + 1])
            path.write_text('<trace-toc><run><data><table schema="SystemPowerLevel"/></data></run></trace-toc>'
                            if "--toc" in command else '<trace-query-result><node><schema name="SystemPowerLevel"/></node></trace-query-result>')

        with tempfile.TemporaryDirectory() as directory, patch.object(power.subprocess, "run", side_effect=export):
            result = power.export_trace(Path("saved.trace"), Path(directory))
            self.assertEqual(result["status"], "failed")
            self.assertEqual(result["metrics_status"], "unavailable")
            self.assertEqual(result["tables"][0]["rows"], 0)

    def test_discovers_multiple_runs_and_preserves_raw_units(self):
        toc = ('<trace-toc><run number="1"><data><table schema="power-v27"/></data></run>'
               '<run number="2"><data><table schema="cpu-impact"/></data></run></trace-toc>')
        commands = []

        def export(command, **kwargs):
            commands.append(command)
            path = Path(command[command.index("--output") + 1])
            path.write_text(toc if "--toc" in command else '<trace-query-result><unit>%/h</unit></trace-query-result>')

        with tempfile.TemporaryDirectory() as directory, patch.object(power.subprocess, "run", side_effect=export):
            out = Path(directory) / "export"
            result = power.export_trace(Path("saved.trace"), out)
            self.assertEqual(result["status"], "exported")
            self.assertEqual(result["metrics_status"], "unvalidated")
            self.assertEqual([t["schema"] for t in result["tables"]], ["power-v27", "cpu-impact"])
            self.assertIn("/trace-toc/run[2]/data/table[1]", commands[2])
            self.assertIn("%/h", Path(result["tables"][0]["path"]).read_text())
            self.assertTrue((out / "manifest.json").exists())

    def test_partial_export_is_failure_and_keeps_successful_tables(self):
        def export(command, **kwargs):
            path = Path(command[command.index("--output") + 1])
            if "--toc" in command:
                path.write_text('<trace-toc><run><data><table schema="a"/><table schema="b"/></data></run></trace-toc>')
            elif "table[1]" in command[command.index("--xpath") + 1]:
                raise subprocess.CalledProcessError(1, command, stderr="unsupported schema")
            else:
                path.write_text('<trace-query-result/>')

        with tempfile.TemporaryDirectory() as directory, patch.object(power.subprocess, "run", side_effect=export):
            result = power.export_trace(Path("saved.trace"), Path(directory))
            self.assertEqual(result["status"], "failed")
            self.assertEqual([t["exported"] for t in result["tables"]], [False, True])
            self.assertIn("unsupported schema", result["errors"])

    def test_empty_or_invalid_toc_never_counts_as_exported(self):
        for toc in ('<trace-toc/>', 'invalid XML'):
            def export(command, **kwargs):
                Path(command[command.index("--output") + 1]).write_text(toc)
            with tempfile.TemporaryDirectory() as directory, patch.object(power.subprocess, "run", side_effect=export):
                result = power.export_trace(Path("saved.trace"), Path(directory))
                self.assertEqual(result["status"], "failed")
                self.assertTrue(result["errors"])


if __name__ == "__main__":
    unittest.main()
