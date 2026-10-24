#!/usr/bin/env python3
"""Export a saved Instruments power trace offline; no phone connection required."""

import argparse
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

POWER_SCHEMAS = {"SystemPowerLevel", "ProcessSubsystemPowerImpact"}


def export_trace(trace, out):
    """Discover exportable tables instead of assuming an Xcode-specific power schema."""
    out.mkdir(parents=True, exist_ok=True)
    toc = out / "toc.xml"
    result = {"trace": str(trace), "status": "failed", "tables": [], "errors": [],
              "metrics_status": "unvalidated", "units": "preserved in exported XML"}
    try:
        subprocess.run(["xcrun", "xctrace", "export", "--input", str(trace), "--toc",
                        "--output", str(toc)], check=True, capture_output=True, text=True)
        root = ET.parse(toc).getroot()
        for run_index, run in enumerate(root.findall("run"), 1):
            for table_index, table in enumerate(run.findall("data/table"), 1):
                path = out / f"run-{run_index}-table-{table_index}.xml"
                entry = {"schema": table.get("schema"), "path": str(path), "exported": False}
                result["tables"].append(entry)
                xpath = f"/trace-toc/run[{run_index}]/data/table[{table_index}]"
                try:
                    subprocess.run(["xcrun", "xctrace", "export", "--input", str(trace),
                                    "--xpath", xpath, "--output", str(path)],
                                   check=True, capture_output=True, text=True)
                    exported = ET.parse(path).getroot()
                    entry["rows"] = len(exported.findall(".//row"))
                    entry["exported"] = True
                except (subprocess.CalledProcessError, OSError, ET.ParseError) as error:
                    result["errors"].append(str(getattr(error, "stderr", None) or error))
        if not result["tables"]:
            result["errors"].append("No exportable tables; inspect the trace in Instruments.")
        power = [table for table in result["tables"] if table["schema"] in POWER_SCHEMAS]
        if power:
            result["metrics_status"] = "available_unvalidated" if any(t.get("rows", 0) for t in power) else "unavailable"
            if result["metrics_status"] == "unavailable":
                result["errors"].append("Power tables contain no samples; this is not zero consumption.")
        if result["tables"] and not result["errors"]:
            result["status"] = "exported"
    except (subprocess.CalledProcessError, OSError, ET.ParseError) as error:
        result["errors"].append(str(getattr(error, "stderr", None) or error))
    (out / "manifest.json").write_text(json.dumps(result, indent=2))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--out", type=Path, required=True, help="new export directory")
    args = parser.parse_args()
    if args.out.exists():
        parser.error("--out must not already exist; use a new directory for a retry")
    result = export_trace(args.trace, args.out)
    print(json.dumps(result, indent=2))
    return 0 if result["status"] == "exported" else 1


if __name__ == "__main__":
    raise SystemExit(main())
