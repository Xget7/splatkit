#!/usr/bin/env python3
"""Rebuild comparison.png from the archived reports and console logs beside this file.

Requires matplotlib. Example: python3 docs/benchmarks/2026-10-24-metal-visibility-12m-ab/plot.py
"""
import datetime as dt
import hashlib
import json
from pathlib import Path
import re
import statistics as stats

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parent
LABELS = ['A1', 'B1', 'B2', 'A2']
SAMPLE = re.compile(r'([\d.]+) fps, gpu ([\d.]+) ms, sort ([\d.]+) ms, cull ([\d.]+) ms, select ([\d.]+) ms, (\d+) drawn of (\d+) selected of (\d+).*yaw ([\d.-]+) pitch')


def samples(path):
    start, rows = None, []
    for line in path.read_text().splitlines():
        if 'benchmark started:' in line:
            start = dt.datetime.fromisoformat(line.split(' SplatKitDev[')[0])
        match = SAMPLE.search(line)
        if start is None or not match:
            continue
        elapsed = (dt.datetime.fromisoformat(line.split(' SplatKitDev[')[0]) - start).total_seconds()
        if not 0 <= elapsed <= 30:
            continue
        v = list(map(float, match.groups()))
        rows.append(dict(seconds=elapsed, fps=v[0], gpu_ms=v[1], visibility_sort_ms=v[2],
                         select_ms=v[4], drawn=int(v[5]), selected=int(v[6]), yaw=v[8]))
    return rows


records = {}
for label in LABELS:
    report = json.loads((ROOT / f'{label}.json').read_text())
    assert report['status'] == 'passed' and report['run']['seconds'] == 30
    assert not report['run']['validation']
    records[label] = dict(report=report, samples=samples(ROOT / f'{label}.log'),
                          log_sha256=hashlib.sha256((ROOT / f'{label}.log').read_bytes()).hexdigest())

metrics = {}
for name, field in [('fps', 'fps_mean'), ('gpu_ms', 'gpu_mean_ms'), ('visibility_sort_ms', 'sort')]:
    values = {}
    for variant, labels in [('before', ['A1', 'A2']), ('change', ['B1', 'B2'])]:
        values[variant] = [stats.mean(r['visibility_sort_ms'] for r in records[label]['samples'] if r['drawn'] > 0 and r['visibility_sort_ms'] > 0)
                           if field == 'sort' else records[label]['report']['runs'][0][field] for label in labels]
    before, after = stats.mean(values['before']), stats.mean(values['change'])
    metrics[name] = dict(**values, before_mean=before, change_mean=after,
                         delta=after-before, delta_percent=100*(after-before)/before)

selected = records['B1']['samples']
low = min(selected, key=lambda row: row['selected'])
peak = max(selected, key=lambda row: row['visibility_sort_ms'])
summary = dict(schema_version=1, order=LABELS, metrics=metrics, runs=records,
               events={'A': dict(label='Lowest sampled LOD', **low),
                       'B': dict(label='Largest sampled visibility + sort interval', **peak)},
               limits=[
                   'Two runs per variant; no significance or sustained-performance claim.',
                   'Same physical phone, world, pose, policy and Release build settings; fixed-position 360-degree turn over 30 seconds.',
                   'GPU values can lag/repeat; GPU ms is the reported renderer interval, not combined GPU queue time.',
                   'Visibility + sort intervals are periodic samples, not isolated visibility timings or all-frame means.',
                   'Adaptive LOD is enabled; selected/drawn counts can vary between runs.',
                   'Stage comparison excludes startup samples with zero drawn splats or zero stage time; original reports retain those samples.',
                   'FPS and GPU summaries include the full benchmark window, including first-frame effects.',
                   'No thermal/memory trace, power profiler, energy, battery or bandwidth measurement.',
                   'Resource monitor warmup was excluded after its 2800 MiB guard stopped the app; both measured variants ran without that optional monitor.',
                   'A/B event markers identify observed samples, not causes.'])
(ROOT / 'comparison.json').write_text(json.dumps(summary, indent=2)+'\n')

BLACK, WHITE, MINT, GRAY = '#000000', '#f5f5f5', '#b9d9cb', '#929292'
plt.rcParams.update({'figure.facecolor': BLACK, 'axes.facecolor': BLACK, 'text.color': WHITE,
                     'axes.labelcolor': GRAY, 'xtick.color': GRAY, 'ytick.color': GRAY,
                     'font.family': 'DejaVu Sans', 'font.size': 11, 'savefig.facecolor': BLACK})
fig = plt.figure(figsize=(14, 11), dpi=160)
fig.text(.065, .952, '12M scene / Metal', fontsize=27, weight='bold')
fig.text(.065, .918, 'iPhone 17 Pro   ·   2 × 30 s per build   ·   A–B–B–A', color=GRAY, fontsize=12)
fig.text(.60, .952, 'No demonstrated gain', fontsize=17, weight='bold', color=MINT)
fig.text(.60, .925, 'Run spread exceeds the mean difference', fontsize=11, color=GRAY)
fig.text(.065, .865, 'Measured change', fontsize=18, weight='bold')

for index, (name, title, unit, direction) in enumerate([
    ('fps', 'Frame rate', 'FPS', 'Higher is better'),
    ('gpu_ms', 'GPU interval', 'ms', 'Lower is better'),
    ('visibility_sort_ms', 'Visibility + sort', 'ms', 'Lower is better')]):
    m = metrics[name]
    ax = fig.add_axes([.065 + index*.315, .625, .275, .175])
    maximum = max(m['before'] + m['change']) * 1.3
    for y, variant, color in [(1, 'before', WHITE), (0, 'change', MINT)]:
        mean = stats.mean(m[variant])
        ax.barh(y, mean, height=.34, color=color, alpha=.22)
        ax.plot(m[variant], [y-.07, y+.07], 'o', color=color, markersize=5)
        ax.plot([min(m[variant]), max(m[variant])], [y,y], color=color, lw=1)
        ax.text(mean+.02*maximum, y+.23, f'{mean:.2f} {unit}', color=color, fontsize=12, weight='bold')
    ax.set_xlim(0, maximum); ax.set_ylim(-.45, 1.65)
    ax.set_yticks([1,0], ['Before','Change']); ax.set_xticks([])
    ax.tick_params(axis='y', length=0, labelcolor=WHITE)
    for spine in ax.spines.values(): spine.set_visible(False)
    ax.set_title(title, loc='left', fontsize=14, weight='bold', pad=23)
    fig.text(.065 + index*.315, .587, f"{m['delta_percent']:+.1f}%   ({m['delta']:+.2f} {unit})", fontsize=16, color=MINT)
    fig.text(.065 + index*.315, .561, direction, fontsize=10, color=GRAY)

fig.text(.065, .497, 'What happened during B1', fontsize=18, weight='bold')
fig.text(.065, .467, f"A  Lowest sampled LOD: {low['selected']/1e6:.2f}M at {low['seconds']:.1f} s", fontsize=11, color=GRAY)
fig.text(.51, .467, f"B  Largest sampled stage: {peak['visibility_sort_ms']:.1f} ms at {peak['seconds']:.1f} s", fontsize=11, color=GRAY)
ax = fig.add_axes([.085, .285, .85, .14])
counts = fig.add_axes([.085, .115, .85, .12], sharex=ax)
x = [r['seconds'] for r in selected]
ax.plot(x, [r['visibility_sort_ms'] for r in selected], 'o-', color=WHITE, lw=1.6, ms=4)
counts.fill_between(x, [r['selected']/1e6 for r in selected], color=MINT, alpha=.16)
counts.plot(x, [r['selected']/1e6 for r in selected], 'o-', color=MINT, lw=1.6, ms=4)
ax.set_ylabel('Visibility + sort (ms)'); counts.set_ylabel('Selected (M)')
ax.tick_params(labelbottom=False); counts.set_xlabel('Seconds into the camera turn')
for panel in [ax, counts]:
    panel.set_xlim(0,30); panel.set_ylim(bottom=0)
    panel.grid(axis='y', color='#222222', linewidth=.6)
    for spine in panel.spines.values(): spine.set_visible(False)
    for letter, event in [('A',low), ('B',peak)]:
        panel.axvline(event['seconds'], color=GRAY, linestyle=':', linewidth=1)
        if panel is ax:
            panel.text(event['seconds'], 1.06, letter, transform=panel.get_xaxis_transform(), ha='center', weight='bold')
counts.set_xticks([0,5,10,15,20,25,30])
fig.text(.065, .035, 'Dots = individual runs above; periodic samples below. Events show correlation, not cause.', fontsize=10, color=GRAY)
fig.savefig(ROOT / 'comparison.png', dpi=160)
print(json.dumps(metrics, indent=2))
