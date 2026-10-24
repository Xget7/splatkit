#!/usr/bin/env python3
"""Plan/run the same declared GPU experiment on Metal or Vulkan; compare offline.

Uses prepared builds and existing platform runners. No build, checkout or implicit target selection.
Timing, timeline and power are separate experiments; native frame capture follows docs/GPU_PROFILING.md.
"""

import argparse
import hashlib
import json
import math
import re
import statistics
import subprocess
import sys
from collections import Counter
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
DEFAULT = ROOT / "scripts/benchmarks/reference-12m.json"


def positive(value, name, maximum=None):
    if (
        isinstance(value, bool)
        or not isinstance(value, (int, float))
        or not math.isfinite(value)
        or value <= 0
        or (maximum and value > maximum)
    ):
        raise ValueError(
            f"{name} must be positive, finite"
            + (f" and <= {maximum}" if maximum else "")
        )


def plan(config, target, phase):
    if config.get("schema_version") != 1:
        raise ValueError("unsupported schema_version")
    s, t = config["scenario"], config["targets"][target]
    if t["backend"] not in ("metal", "vulkan"):
        raise ValueError("backend must be metal or vulkan")
    if phase not in ("timing", "timeline", "power"):
        raise ValueError("unsupported experiment phase")
    if phase == "power" and t["backend"] != "metal":
        raise ValueError(
            "Android energy needs supported rail/counter collection; this runner does not measure it"
        )
    positive(s["seconds"], "seconds", 3600)
    positive(s["warmup_seconds"], "warmup_seconds", 3600)
    positive(s["budget"], "budget")
    if not isinstance(s["budget"], int):
        raise TypeError("budget must be an integer")
    if (
        not isinstance(s["subpixel"], (int, float))
        or isinstance(s["subpixel"], bool)
        or not math.isfinite(s["subpixel"])
        or s["subpixel"] < 0
    ):
        raise ValueError("subpixel must be finite and nonnegative")
    if (
        not isinstance(s["pose"], list)
        or len(s["pose"]) != 5
        or any(
            isinstance(v, bool)
            or not isinstance(v, (int, float))
            or not math.isfinite(v)
            for v in s["pose"]
        )
    ):
        raise ValueError("pose must contain five finite numbers")
    # Both native benchmark turns reset heading and pitch to zero.
    if s["pose"][3:] != [0, 0]:
        raise ValueError("benchmark turn resets yaw/pitch: declare both as zero")
    if (
        not s["world"]
        or s.get("source_splats") != 11988085
        or s.get("prepared_nodes") != 13375264
    ):
        raise ValueError("use the declared 12M reference world")
    counts = Counter(s["order"])
    if (
        set(counts) != {"before", "change"}
        or counts["before"] != counts["change"]
        or counts["before"] < 2
    ):
        raise ValueError(
            "order must balance before/change with at least two repeats each"
        )
    qualities = {
        "metal": ("ultra", "high", "balanced", "fast"),
        "vulkan": ("low", "medium", "high", "ultra"),
    }
    if t["quality"] not in qualities[t["backend"]]:
        raise ValueError("unsupported backend quality")
    if set(t["builds"]) != {"before", "change"}:
        raise ValueError("builds must contain before and change")
    runs, seen = [], set()
    order = s["order"] if phase == "timing" else ["before", "change"]
    for index, variant in enumerate(order):
        if phase == "timing" and variant not in seen:
            runs.append(
                {
                    "label": f"warmup-{variant}",
                    "variant": variant,
                    "phase": "warmup",
                    "seconds": s["warmup_seconds"],
                }
            )
            seen.add(variant)
        runs.append(
            {
                "label": f"{index + 1:02d}-{variant}",
                "variant": variant,
                "phase": phase,
                "seconds": s["seconds"],
            }
        )
    previous = None
    for item in runs:
        item["install"] = item["variant"] != previous
        previous = item["variant"]
    return {
        "schema_version": 1,
        "target": target,
        "backend": t["backend"],
        "device": t["device"],
        "scenario": s,
        "quality": t["quality"],
        "builds": t["builds"],
        "phase": phase,
        "runs": runs,
        "active_seconds": sum(r["seconds"] for r in runs),
        "scope": "active camera turns only; excludes install, load, cooldown, export and trace-save time",
        "instrumentation": "Perfetto for every Vulkan run; Metal System Trace only in timeline; Power Profiler only in power",
    }


def artifact_hash(path, backend):
    binary = path / "SplatKitDev" if backend == "metal" else path
    if not binary.is_file():
        raise ValueError(f"prepared build missing: {binary}")
    digest = hashlib.sha256()
    with binary.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def execute_run(experiment, item, artifact, out):
    s = experiment["scenario"]
    common = {
        "seconds": item["seconds"],
        "quality": experiment["quality"],
        "pose": ",".join(str(v) for v in s["pose"]),
        "world": s["world"],
        "budget": s["budget"],
        "subpixel": s["subpixel"],
        "validation": False,
        "out": out,
        "skip_build": True,
    }
    if experiment["backend"] == "metal":
        import ios_perf

        previous = ios_perf.APP
        try:
            ios_perf.APP = artifact
            return ios_perf.run(
                SimpleNamespace(
                    **common,
                    udid=experiment["device"],
                    trace=item["phase"] == "timeline",
                    power=item["phase"] == "power",
                    skip_install=not item["install"],
                )
            )
        finally:
            ios_perf.APP = previous
    import android_perf

    previous = android_perf.apk
    try:
        android_perf.apk = lambda variant: artifact
        return android_perf.run(
            SimpleNamespace(
                **common,
                serial=experiment["device"],
                lod_splat_limit=0,
                skip_install=not item["install"],
                gpu_stages=item["phase"] == "timeline",
            )
        )
    finally:
        android_perf.apk = previous


def run(experiment, out):
    if not experiment["device"] or experiment["device"].startswith("SET_"):
        raise ValueError("set an explicit physical device identifier before running")
    # Validate both prepared builds before any phone action.
    for build in experiment["builds"].values():
        if not re.fullmatch(r"[0-9a-f]{40}", build["commit"]):
            raise ValueError("each build requires its full declared source commit")
        path = Path(build["artifact"])
        if not path.is_absolute():
            path = ROOT / path
        build["artifact"] = str(path.resolve())
        build["sha256"] = artifact_hash(path, experiment["backend"])
    out.mkdir(parents=True, exist_ok=False)
    manifest = out / "experiment.json"
    experiment["status"] = "running"
    manifest.write_text(json.dumps(experiment, indent=2) + "\n")
    try:
        for item in experiment["runs"]:
            directory = out / item["label"]
            build = experiment["builds"][item["variant"]]
            if (
                artifact_hash(Path(build["artifact"]), experiment["backend"])
                != build["sha256"]
            ):
                raise RuntimeError("prepared build changed during the experiment")
            code = execute_run(experiment, item, Path(build["artifact"]), directory)
            report_path = directory / "report.json"
            report = json.loads(report_path.read_text())
            if report.get("runs") and any(
                r.get("resident_records") != experiment["scenario"]["prepared_nodes"]
                for r in report["runs"]
            ):
                code = 1
                report["status"] = "failed"
                report.setdefault("errors", []).append(
                    "prepared node count does not match reference world"
                )
            report["installed_build"] = experiment["builds"][item["variant"]]
            report["experiment"] = {
                "target": experiment["target"],
                "phase": item["phase"],
                "variant": item["variant"],
            }
            report_path.write_text(json.dumps(report, indent=2) + "\n")
            item["report"] = str(report_path.relative_to(out))
            item["status"] = report["status"]
            manifest.write_text(json.dumps(experiment, indent=2) + "\n")
            if code or report["status"] != "passed":
                raise RuntimeError(
                    f"run failed: {item['label']}; inspect {report_path}"
                )
        experiment["status"] = "complete"
    except BaseException:
        experiment["status"] = "failed"
        raise
    finally:
        manifest.write_text(json.dumps(experiment, indent=2) + "\n")
    compare(manifest)


def compare(manifest):
    experiment = json.loads(manifest.read_text())
    if experiment["phase"] != "timing":
        return (
            None  # Instrumented experiments are never ranked as performance A/B runs.
        )
    if experiment["status"] != "complete":
        raise ValueError("only complete timing experiments can be compared")
    data = {"before": [], "change": []}
    for item in experiment["runs"]:
        if item["phase"] == "warmup":
            continue
        report = json.loads((manifest.parent / item["report"]).read_text())
        if (
            report["status"] != "passed"
            or report["run"]["validation"]
            or len(report["runs"]) != 1
            or "power" in report
            or (experiment["backend"] == "metal" and "trace" in report)
        ):
            raise ValueError(
                "comparison requires one valid uninstrumented benchmark window per report"
            )
        data[item["variant"]].append(report["runs"][0])
    if any(len(values) < 2 for values in data.values()) or len(data["before"]) != len(
        data["change"]
    ):
        raise ValueError("comparison requires balanced repeated runs")
    result = {
        "backend": experiment["backend"],
        "device": experiment["device"],
        "metrics": {},
        "scope": "Descriptive repeats only; no significance, sustained or causal claim. Compare within backend/device.",
        "unmeasured": ["battery", "energy", "bandwidth"],
        "native_frames": "not collected by this runner",
    }
    for key in ("fps_mean", "frame_p95_ms", "gpu_mean_ms", "sort_sample_mean_ms"):
        if key == "sort_sample_mean_ms":
            for runs in data.values():
                for row in runs:
                    row[key] = row.get("stage_valid_sample_mean_ms", {}).get("sort")
        groups = {v: [r.get(key) for r in data[v]] for v in data}
        if any(value is None for values in groups.values() for value in values):
            result["metrics"][key] = {"status": "unavailable"}
            continue
        before, change = (statistics.mean(groups[v]) for v in ("before", "change"))
        result["metrics"][key] = dict(
            status="measured",
            **groups,
            before_mean=before,
            change_mean=change,
            delta=change - before,
            delta_percent=100 * (change - before) / before if before else None,
        )
    fps = result["metrics"]["fps_mean"]
    if fps["status"] == "measured":
        repeat_range = max(max(fps[v]) - min(fps[v]) for v in ("before", "change"))
        delta = fps["delta"]
        if abs(delta) <= repeat_range:
            takeaway = "Mean difference is smaller than the repeat range; no demonstrated gain."
        elif delta > 0:
            takeaway = "Observed FPS gain; thermal/image-quality attribution remains unvalidated."
        else:
            takeaway = "Observed FPS slowdown; thermal/image-quality attribution remains unvalidated."
        result["takeaway"] = takeaway
        result["fps_repeat_range"] = repeat_range
        result["optimization_decision"] = (
            "not_accepted" if delta <= repeat_range else "needs_validation"
        )
    result["acceptance_scope"] = (
        "FPS optimization only; do not accept a change without demonstrated end-to-end benefit or with unresolved critical-stage regressions."
    )
    sort = result["metrics"]["sort_sample_mean_ms"]
    if sort["status"] == "measured":
        sort_range = max(max(sort[v]) - min(sort[v]) for v in ("before", "change"))
        if sort["delta"] > sort_range:
            result["optimization_decision"] = "not_accepted"
            result["stage_warning"] = (
                "Sort-stage slowdown exceeds the observed repeat range; investigate before accepting an optimization."
            )
    (manifest.parent / "comparison.json").write_text(
        json.dumps(result, indent=2) + "\n"
    )
    (manifest.parent / "comparison.md").write_text(markdown(result) + "\n")
    return result


def markdown(result):
    """GitHub-native comparison; unavailable measurements never become zero."""

    def cell(text):
        return str(text).replace("|", "\\|").replace("\n", " ")

    rows = [
        f"**{cell(result['backend'].title())} / {cell(result['device'])}**",
        "",
        result.get("takeaway", "No FPS conclusion available."),
        "",
        "| Metric | Before mean (range) | Change mean (range) | Delta | Delta % |",
        "| --- | ---: | ---: | ---: | ---: |",
    ]
    labels = {
        "fps_mean": "FPS - higher is better",
        "frame_p95_ms": "Frame interval p95 (ms) - lower is better",
        "gpu_mean_ms": "GPU interval (ms) - lower is better",
        "sort_sample_mean_ms": (
            "Visibility + sort" if result["backend"] == "metal" else "Sort stage"
        )
        + " (sampled ms) - lower is better",
    }
    for key, label in labels.items():
        metric = result["metrics"][key]
        if metric["status"] != "measured":
            rows.append(f"| {label} | Unmeasured | Unmeasured | - | - |")
            continue
        values = []
        for variant in ("before", "change"):
            repeated = metric[variant]
            values.append(
                f"{metric[variant + '_mean']:.2f} ({min(repeated):.2f}-{max(repeated):.2f})"
            )
        percent = (
            "-"
            if metric["delta_percent"] is None
            else f"{metric['delta_percent']:+.1f}%"
        )
        rows.append(
            f"| {label} | {values[0]} | {values[1]} | {metric['delta']:+.2f} | {percent} |"
        )
    rows += [
        "",
        f"**FPS optimization: {result.get('optimization_decision', 'unvalidated').replace('_', ' ')}.**",
    ]
    if "stage_warning" in result:
        rows += ["", result["stage_warning"]]
    rows += [
        "",
        "Ranges describe individual run summaries; they are not confidence intervals.",
        "Stage values exclude unavailable zero/startup samples; they are periodic observations, not isolated kernel timings.",
        "Battery, energy and bandwidth: unmeasured.",
        "Thermal comparability, native frame analysis and causal attribution remain unvalidated.",
        "Source: [comparison.json](comparison.json), [run manifest](experiment.json).",
    ]
    return "\n".join(rows)


def plot(manifest):
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    result = compare(manifest)
    if result is None:
        raise ValueError("only timing experiments have a comparison chart")
    fig, axes = plt.subplots(1, 3, figsize=(13, 4.5), facecolor="#000000")
    for ax, (key, title, unit, direction) in zip(
        axes,
        [
            ("fps_mean", "Frame rate", "FPS", "Higher is better"),
            ("frame_p95_ms", "Frame interval p95", "ms", "Lower is better"),
            ("gpu_mean_ms", "GPU interval mean", "ms", "Lower is better"),
        ],
    ):
        ax.set_facecolor("#000000")
        metric = result["metrics"][key]
        ax.set_title(title, color="white", loc="left", fontsize=13, pad=20)
        if metric["status"] == "unavailable":
            ax.text(0.1, 0.5, "Unmeasured", color="#999999", transform=ax.transAxes)
        else:
            for y, variant, color in [
                (1, "before", "#eeeeee"),
                (0, "change", "#b9d9cb"),
            ]:
                values = metric[variant]
                mean = metric[variant + "_mean"]
                ax.barh(y, mean, height=0.3, color=color, alpha=0.25)
                ax.scatter(values, [y] * len(values), s=24, color=color)
                ax.text(0, y + 0.27, f"{mean:.2f} {unit}", color=color, fontsize=13)
            delta = metric["delta_percent"]
            ax.set_xlabel(
                f"{delta:+.1f}% / {metric['delta']:+.2f} {unit}\n{direction}"
                if delta is not None
                else "Baseline zero; percentage unavailable",
                color="#bbbbbb",
                labelpad=15,
            )
        ax.set_yticks([1, 0], ["Before", "Change"], color="white")
        ax.set_xticks([])
        ax.set_ylim(-0.4, 1.7)
        for spine in ax.spines.values():
            spine.set_visible(False)
    fig.suptitle(
        f"12M / {result['backend'].title()} / {result['device']}",
        color="white",
        fontsize=16,
        x=0.08,
        ha="left",
    )
    fig.text(
        0.08,
        0.02,
        "Dots show repeats. Descriptive results; no causal or sustained-performance claim.",
        color="#999999",
        fontsize=9,
    )
    fig.subplots_adjust(left=0.08, right=0.98, top=0.78, bottom=0.23, wspace=0.4)
    fig.savefig(manifest.parent / "comparison.png", dpi=160, facecolor="#000000")
    plt.close(fig)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("action", choices=("plan", "run", "compare", "plot"))
    p.add_argument("--config", type=Path, default=DEFAULT)
    p.add_argument("--target", choices=("metal", "vulkan"))
    p.add_argument("--phase", choices=("timing", "timeline", "power"), default="timing")
    p.add_argument("--out", type=Path)
    p.add_argument("--manifest", type=Path)
    args = p.parse_args()
    try:
        if args.action in ("compare", "plot"):
            if not args.manifest:
                p.error("compare/plot requires --manifest")
            if args.action == "plot":
                plot(args.manifest)
            else:
                print(json.dumps(compare(args.manifest), indent=2))
            return 0
        if not args.target:
            p.error("plan/run requires --target")
        experiment = plan(json.loads(args.config.read_text()), args.target, args.phase)
        if args.action == "plan":
            print(json.dumps(experiment, indent=2))
            return 0
        if not args.out:
            p.error("run requires a new --out directory")
        run(experiment, args.out)
        return 0
    except (
        OSError,
        ValueError,
        TypeError,
        KeyError,
        RuntimeError,
        ImportError,
        subprocess.CalledProcessError,
        subprocess.TimeoutExpired,
    ) as error:
        print(f"gpu_benchmark: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
