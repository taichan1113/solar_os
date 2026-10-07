"""Analyze one uninterrupted Writer/async generation epoch; timestamps are us.

Replaced frames are mapped only through explicit replacement chains. A later
unrelated completion never proves that a cancelled or failed frame was shown.
Panel completion means driver BUSY completion, not an optical measurement.
"""
import argparse
import json
import re
import statistics
from pathlib import Path


def analyze(text):
    events, seen, malformed = [], set(), []
    required = {
        'KEY_REPORT': {'t', 'key'},
        'WRITER_INPUT': {'frame', 't', 'key'},
        'MUTATION_END': {'frame', 't'},
        'WRITER_RENDER_START': {'frame', 't'},
        'WRITER_RENDER_END': {'frame', 't'},
        'SUBMIT_RETURN': {'frame', 't', 'gen', 'err'},
        'SNAPSHOT_SUBMIT': {'gen', 't', 'copied_us', 'replaced'},
        'WORKER_START': {'gen', 't'},
        'WORKER_END': {'gen', 't', 'err', 'physical'},
    }
    pattern = r"(?:KEY_REPORT|frame=\d+ (?:WRITER_INPUT|MUTATION_END|WRITER_RENDER_START|WRITER_RENDER_END|SUBMIT_RETURN)|gen=\d+ (?:SNAPSHOT_SUBMIT|WORKER_START|WORKER_END))\b.*"
    for line in text.splitlines():
        match = re.search(pattern, line)
        if not match or match[0] in seen:
            continue
        seen.add(match[0])
        values = {k: int(v) for k, v in re.findall(r"(\w+)=(-?\d+)", match[0])}
        values['event'] = re.search(r"\b(KEY_REPORT|WRITER_INPUT|MUTATION_END|WRITER_RENDER_START|WRITER_RENDER_END|SUBMIT_RETURN|SNAPSHOT_SUBMIT|WORKER_START|WORKER_END)\b", match[0])[0]
        missing = required[values['event']] - values.keys()
        if missing:
            # Concurrent native/foreground serial output can interleave. Keep
            # the evidence visible; never manufacture timestamps or completion.
            malformed.append(dict(record=match[0], missing=sorted(missing)))
            continue
        events.append(values)
    events.sort(key=lambda e: e['t'])
    by = lambda name: [e for e in events if e['event'] == name]
    reports = by('KEY_REPORT')
    inputs = by('WRITER_INPUT')
    for inp in inputs:
        mutation = next((m for m in by('MUTATION_END') if m['frame'] == inp['frame'] and m['t'] >= inp['t']), None)
        inp['mutation_t'] = mutation['t'] if mutation else None
        report = next((r for r in reports if not r.get('used') and r['key'] == inp['key'] and r['t'] <= inp['t']), None)
        if report:
            report['used'] = True
            inp['report_t'] = report['t']
            inp['dispatch_us'] = inp['t'] - report['t']
    snapshots = by('SNAPSHOT_SUBMIT')
    replacements = {s['replaced']: s['gen'] for s in snapshots if s['replaced']}
    starts = {s['gen']: s for s in by('WORKER_START')}
    ends = {s['gen']: s for s in by('WORKER_END')}
    frames = []
    for submit in by('SUBMIT_RETURN'):
        frame, gen = submit['frame'], submit['gen']
        start = next((s for s in by('WRITER_RENDER_START') if s['frame'] == frame and s['t'] <= submit['t']), None)
        end = next((s for s in by('WRITER_RENDER_END') if s['frame'] == frame and s['t'] <= submit['t']), None)
        if not start or not end:
            continue
        chain, visited = gen, set()
        while chain in replacements and chain not in visited:
            visited.add(chain)
            chain = replacements[chain]
        done = ends.get(chain) if submit['err'] == 0 else None
        completed = done['t'] if done and done['err'] == 0 else None
        batch = [i for i in inputs if i['frame'] == frame and i['t'] <= start['t']]
        for inp in batch:
            inp['catchup_us'] = completed - inp['report_t'] if completed is not None and 'report_t' in inp else None
        frames.append(dict(frame=frame, gen=gen, inputs=len(batch), render_start_us=start['t'],
                           submit_return_us=submit['t'], render_us=end['t']-start['t'],
                           submit_us=submit['t']-end['t'], catchup_gen=chain if completed else None,
                           completed_us=completed, replaced=gen in replacements,
                           first_dispatch_us=batch[0].get('dispatch_us') if batch else None))
    workers = [dict(gen=g, start_us=s['t'], end_us=ends[g]['t'], duration_us=ends[g]['t']-s['t'],
                    error=ends[g]['err'], physical=ends[g]['physical']) for g, s in starts.items() if g in ends]
    for inp in inputs:
        inp['received_while_worker_busy'] = 'report_t' in inp and any(
            w['physical'] and w['start_us'] <= inp['report_t'] < w['end_us'] for w in workers)
    groups = []
    for inp in (i for i in inputs if 'report_t' in i):
        if not groups or inp['report_t'] - groups[-1][-1]['report_t'] > 4_000_000:
            groups.append([])
        groups[-1].append(inp)
    trials = []
    for group in groups:
        frame_ids = {i['frame'] for i in group}
        selected = [f for f in frames if f['frame'] in frame_ids]
        complete = max((f['completed_us'] for f in selected if f['completed_us'] is not None), default=None)
        if len(selected) != len(frame_ids) or any(f['completed_us'] is None for f in selected):
            complete = None
        gens = {f['gen'] for f in selected}
        trials.append(dict(inputs=len(group), keys=''.join(chr(i['key']) if 32 <= i['key'] < 127 else f"<{i['key']}>" for i in group),
                           frame_ids=sorted(frame_ids),
                           first_report_us=group[0]['report_t'], last_report_us=group[-1]['report_t'],
                           renders=len(selected), submits=len(gens), replaced=sum(f['replaced'] for f in selected),
                           physical=sum(w['physical'] for w in workers if w['gen'] in gens and not w['error']),
                           first_report_to_complete_us=complete-group[0]['report_t'] if complete else None,
                           last_report_to_complete_us=complete-group[-1]['report_t'] if complete else None))
    def stats(values):
        values = [v for v in values if v is not None]
        return dict(n=len(values), min=min(values), median=statistics.median(values), max=max(values)) if values else None
    return dict(counts=dict(inputs=len(inputs), matched_reports=sum('report_t' in i for i in inputs),
                            renders=len(by('WRITER_RENDER_START')), submits=len(snapshots),
                            replaced=len(replacements), workers=len(workers),
                            physical=sum(w['physical'] for w in workers if w['error'] == 0)),
                dispatch_us=stats([i.get('dispatch_us') for i in inputs]),
                busy_dispatch_us=stats([i.get('dispatch_us') for i in inputs if i['received_while_worker_busy']]),
                first_batch_dispatch_us=stats([f['first_dispatch_us'] for f in frames]),
                render_us=stats([f['render_us'] for f in frames]),
                submit_us=stats([f['submit_us'] for f in frames]),
                copy_us=stats([s['copied_us'] for s in snapshots]),
                worker_us=stats([w['duration_us'] for w in workers if w['physical'] and not w['error']]),
                trials=trials, frames=frames, inputs=inputs, workers=workers,
                malformed_events=malformed)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--json', type=Path)
    args = parser.parse_args()
    result = analyze(args.log.read_text(encoding='utf-8', errors='replace'))
    if args.json:
        args.json.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in result.items() if k not in ('frames', 'inputs', 'workers')}, indent=2))
