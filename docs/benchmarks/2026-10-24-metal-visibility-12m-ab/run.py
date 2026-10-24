"""Reproduce one run using a prepared Release app; run from the repository root.

Stage builds as build/pr60-ab/{base,change}.app and their source revisions in
{base,change}-commit.txt; see comparison.json for recorded revisions and binary hashes.
This keeps compilation off the physical-phone budget and serializes all device runs.
Usage: python3 docs/benchmarks/2026-10-24-metal-visibility-12m-ab/run.py base A1
"""
import argparse,hashlib,json,sys
from pathlib import Path
sys.path.insert(0,str(Path('scripts').resolve()))
import ios_perf as perf
p=argparse.ArgumentParser();p.add_argument('variant',choices=['base','change']);p.add_argument('label');p.add_argument('--seconds',type=float,default=30);args=p.parse_args()
bank=Path('build/pr60-ab').resolve();perf.APP=bank/(args.variant+'.app')
commit=(bank/(args.variant+'-commit.txt')).read_text().strip()
binary=perf.APP/'SplatKitDev'
assert binary.exists()
data=binary.read_bytes();guard=b'if (kIndexedLOD && t - groupThread >= total) return;'
assert (guard in data)==(args.variant=='change'), 'Wrong shader embedded in staged binary'
out=Path('output/ios-perf/pr60-ab-12m-2026-10-24')/args.label
run=argparse.Namespace(udid='00008150-0019719C34E2401C',seconds=args.seconds,quality='high',pose='-7.25,1.6,5.25,0,0',world='scene.lodsplat',budget=1800000,subpixel=1.0,validation=False,trace=False,out=out,skip_build=True)
result=perf.run(run)
report=json.loads((out/'report.json').read_text())
report['run']['driver_commit']=report['run']['commit'];report['run']['commit']=commit
report['run']['binary_sha256']=hashlib.sha256(data).hexdigest();report['run']['variant']=args.variant
report['run']['resource_monitor_hz']=0
report['run']['thermal_trace_available']=False
log=(out/'console.log').read_text()
assert 'Metal API Validation Enabled' not in log and 'Metal GPU Validation Enabled' not in log
assert 'decoded 11988085 splats' in log and 'pos -7.25 1.60 5.25' in log
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
raise SystemExit(result)
