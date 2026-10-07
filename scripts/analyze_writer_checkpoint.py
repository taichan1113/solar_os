"""Correlate checkpoint I/O windows with actual BLE reception and Writer dispatch.

Only reception timestamps inside a completed worker window count as busy samples.
Do not infer overlap from when a user started typing or from a later completion.
"""
import argparse
import json
import re
import statistics
from pathlib import Path
from analyze_display_async import analyze as analyze_input


def stats(values):
    return dict(n=len(values), min=min(values), median=statistics.median(values),
                max=max(values)) if values else None


def analyze(text):
    events, seen = [], set()
    for line in text.splitlines():
        match = re.search(r'gen=\d+ ver=\d+ CHECKPOINT_\w+ t=\d+ err=-?\d+', line)
        if not match or match[0] in seen:
            continue
        seen.add(match[0])
        e = {k: int(v) for k, v in re.findall(r'(\w+)=(-?\d+)', match[0])}
        e['event'] = re.search(r'CHECKPOINT_\w+', match[0])[0]
        events.append(e)
    events.sort(key=lambda e: e['t'])
    windows, active = [], {}
    for e in events:
        key = e['gen'], e['ver']
        if e['event'] == 'CHECKPOINT_WORKER_START':
            active[key] = e['t']
        elif e['event'] in ('CHECKPOINT_COMPLETE', 'CHECKPOINT_ERROR') and key in active:
            start = active.pop(key)
            windows.append(dict(gen=e['gen'], version=e['ver'], start_us=start,
                                end_us=e['t'], duration_us=e['t']-start, error=e['err']))
    inputs = analyze_input(text)['inputs']
    for i in inputs:
        i['checkpoint_busy'] = 'report_t' in i and any(
            w['start_us'] <= i['report_t'] < w['end_us'] for w in windows)
    groups = []
    for i in (i for i in inputs if 'report_t' in i):
        if not groups or i['report_t']-groups[-1][-1]['report_t'] > 4_000_000:
            groups.append([])
        groups[-1].append(i)
    trials = [dict(keys=''.join(chr(i['key']) if 32 <= i['key'] < 127 else
                               f"<{i['key']}>" for i in group),
                   inputs=len(group), busy_inputs=sum(i['checkpoint_busy'] for i in group),
                   start_us=group[0]['report_t'], end_us=group[-1]['report_t'])
              for group in groups]
    copies = [int(re.search(r'copy_us=(\d+)', line)[1]) for line in set(re.findall(
        r'gen=\d+ ver=\d+ SNAPSHOT bytes=\d+ copy_us=\d+', text))]
    return dict(checkpoints=len(windows), unfinished=len(active),
                errors=sum(e['event'] == 'CHECKPOINT_ERROR' for e in events),
                replaced=sum(e['event'] == 'CHECKPOINT_REPLACED_PENDING' for e in events),
                checkpoint_us=stats([w['duration_us'] for w in windows if not w['error']]),
                copy_us=stats(copies),
                dispatch_us=stats([i['dispatch_us'] for i in inputs if 'dispatch_us' in i]),
                busy_dispatch_us=stats([i['dispatch_us'] for i in inputs if i['checkpoint_busy']]),
                idle_dispatch_us=stats([i['dispatch_us'] for i in inputs
                                       if 'dispatch_us' in i and not i['checkpoint_busy']]),
                trials=trials, windows=windows, inputs=inputs)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--json', type=Path)
    args = parser.parse_args()
    result = analyze(args.log.read_text(encoding='utf-8', errors='replace'))
    if args.json:
        args.json.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in result.items() if k not in ('windows', 'inputs')}, indent=2))
