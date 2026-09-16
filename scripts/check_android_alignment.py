#!/usr/bin/env python3
"""Check ELF LOAD alignment in .so/.aar/.apk files and APK ZIP alignment.

Requires NDK llvm-readelf; APK checks also require SDK build-tools zipalign.
This checks distribution artifacts, not runtime behavior on a 16 KB device.
"""

import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import zipfile


def check_load_segments(output):
    segments = [line.split() for line in output.splitlines()
                if line.split() and line.split()[0] == "LOAD"]
    if not segments:
        raise ValueError("ELF has no LOAD segments")
    alignments = []
    for fields in segments:
        if len(fields) < 8:
            raise ValueError("Malformed ELF LOAD header")
        offset, address, alignment = (int(fields[index], 16) for index in (1, 2, -1))
        if alignment < 16384 or alignment & (alignment - 1):
            raise ValueError(f"LOAD alignment {alignment:#x} is not 16 KB compatible")
        if (address - offset) % 16384:
            raise ValueError("LOAD address and file offset are not congruent modulo 16 KB")
        alignments.append(alignment)
    return alignments


def check_relro_segments(output):
    count = 0
    for line in output.splitlines():
        fields = line.split()
        if not fields or fields[0] != "GNU_RELRO":
            continue
        if len(fields) < 8:
            raise ValueError("Malformed ELF GNU_RELRO header")
        address, size = int(fields[2], 16), int(fields[5], 16)
        if (address + size) % 16384:
            raise ValueError("GNU_RELRO end is not aligned to 16 KB")
        count += 1
    return count


def check_elf(path, readelf):
    result = subprocess.run([str(readelf), "-lW", str(path)], check=True,
                            capture_output=True, text=True)
    return {"load_alignments": check_load_segments(result.stdout),
            "relro_segments_checked": check_relro_segments(result.stdout)}


def check_artifact(path, readelf, zipalign=None):
    path = Path(path)
    libraries = []
    if path.suffix == ".so":
        libraries.append({"name": path.name, **check_elf(path, readelf)})
    elif path.suffix in (".aar", ".apk"):
        with zipfile.ZipFile(path) as archive, tempfile.TemporaryDirectory() as directory:
            for entry in archive.infolist():
                if entry.filename.endswith(".so"):
                    # A fixed local filename avoids trusting archive paths.
                    library = Path(directory) / "library.so"
                    library.write_bytes(archive.read(entry))
                    libraries.append({"name": entry.filename,
                                      **check_elf(library, readelf)})
        if not libraries:
            raise ValueError("Archive contains no shared libraries")
        if path.suffix == ".apk":
            if not zipalign:
                raise ValueError("APK checks require --zipalign")
            subprocess.run([str(zipalign), "-c", "-P", "16", "-v", "4", str(path)],
                           check=True, capture_output=True, text=True)
    else:
        raise ValueError("Expected a .so, .aar, or .apk artifact")
    return {"artifact": str(path), "status": "passed", "libraries": libraries,
            "apk_zip_alignment_checked": path.suffix == ".apk",
            "runtime_16kb_validated": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifacts", nargs="+", type=Path)
    parser.add_argument("--readelf", default="llvm-readelf")
    parser.add_argument("--zipalign")
    args = parser.parse_args()
    results = []
    for path in args.artifacts:
        try:
            results.append(check_artifact(path, args.readelf, args.zipalign))
        except (OSError, ValueError, zipfile.BadZipFile, subprocess.CalledProcessError) as error:
            detail = str(error)
            if isinstance(error, subprocess.CalledProcessError):
                detail += "\n" + (error.stderr or error.stdout or "").strip()
            results.append({"artifact": str(path), "status": "failed", "error": detail})
    print(json.dumps(results, indent=2))
    return int(any(result["status"] != "passed" for result in results))


if __name__ == "__main__":
    raise SystemExit(main())
