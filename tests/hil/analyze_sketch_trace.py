"""Validate and summarize a stopped PaperMono Sketch trial journal.

Input is the concatenated output of `sketchtrace stop` and paged
`sketchtrace dump <offset> 128` commands. Times are monotonic microseconds;
driver completion is not an optical appearance timestamp.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import statistics

NAMES = ('', 'READ', 'SAMPLE', 'PUSH', 'POP', 'DISPATCH', 'EVENT',
         'CANVAS', 'RENDER', 'XBM', 'BITMAP', 'UI', 'SUBMIT', 'REPLACE',
         'WORKER_START', 'WORKER_END', 'SSD', 'SUSPEND', 'RESUME',
         'SSD_CONVERT', 'SSD_SPI', 'SSD_BUSY')
MOVE, PRESS, RELEASE = 0, 1, 2


def parse(lines):
    rows, meta = {}, None
    for line in lines:
        line = line.strip()
        if line.startswith('SKTRACE_META,'):
            parts = line.split(',')
            if len(parts) != 6:
                raise ValueError('malformed trace metadata')
            meta = dict(zip(('active', 'count', 'overflow', 'observer_us',
                             'observer_max_us'), map(int, parts[1:])))
        elif line.startswith('SKTRACE,'):
            parts = line.split(',')
            if len(parts) != 9:
                raise ValueError('malformed trace row')
            index, t, kind, point, version, generation, a, b = map(int, parts[1:])
            if kind <= 0 or kind >= len(NAMES) or index in rows:
                raise ValueError('invalid or duplicate trace row')
            rows[index] = dict(index=index, time_us=t, kind=NAMES[kind],
                               point=point, version=version,
                               generation=generation, a=a, b=b)
    if meta is None or meta['active']:
        raise ValueError('stopped trace metadata required')
    if len(rows) != meta['count'] or sorted(rows) != list(range(meta['count'])):
        raise ValueError('missing dump rows; never infer them')
    ordered = [rows[i] for i in range(meta['count'])]
    if any(b['time_us'] < a['time_us'] for a, b in zip(ordered, ordered[1:])):
        raise ValueError('non-monotonic journal')
    return meta, ordered


def distribution(values):
    if not values:
        return None
    values = sorted(values)
    return dict(count=len(values), p50_us=statistics.median(values),
                p95_us=values[math.ceil(len(values)*.95)-1],
                maximum_us=max(values))


def analyze(meta, rows):
    kinds = {name: [r for r in rows if r['kind'] == name] for name in NAMES[1:]}
    by_point = {}
    for row in rows:
        if row['point']:
            by_point.setdefault(row['point'], {}).setdefault(row['kind'], []).append(row)
    samples = kinds['SAMPLE']
    if [r['point'] for r in samples] != list(range(1, len(samples)+1)):
        raise ValueError('point sequence not contiguous')
    submits = {r['generation']: r for r in kinds['SUBMIT']
               if r['generation'] and r['b'] == 0}
    starts = {r['generation']: r for r in kinds['WORKER_START']}
    ends = {r['generation']: r for r in kinds['WORKER_END']}
    replacements = {r['a'] for r in kinds['REPLACE']}
    failures = []
    read_failures = sum(r['b'] != 0 for r in kinds['READ'])
    if read_failures:
        failures.append(f'FT6336 I2C read failed {read_failures} time(s)')
    stroke = 0
    for sample in samples:
        pushes = by_point[sample['point']].get('PUSH', [])
        if pushes and pushes[0]['a'] == PRESS:
            stroke += 1
        if sample['version'] != stroke:
            failures.append(f'point {sample["point"]} stroke ID mismatch')
    stages = {key: [] for key in ('read', 'sample_to_push', 'fifo_wait',
              'event', 'canvas', 'canvas_to_render', 'xbm', 'bitmap', 'ui',
              'submit', 'executor_wait', 'ssd', 'ssd_convert', 'ssd_spi',
              'ssd_busy', 'point_to_canvas',
              'press_to_done', 'release_to_done')}
    stages['read'] = [r['a'] for r in kinds['READ'] if r['b'] == 0]
    for name, key in (('EVENT', 'event'), ('CANVAS', 'canvas'),
                      ('XBM', 'xbm'), ('BITMAP', 'bitmap'), ('UI', 'ui'),
                      ('SUBMIT', 'submit'), ('SSD', 'ssd'),
                      ('SSD_CONVERT', 'ssd_convert'),
                      ('SSD_SPI', 'ssd_spi'), ('SSD_BUSY', 'ssd_busy')):
        stages[key] = [r['a'] for r in kinds[name] if r['a'] >= 0]
    for gen, submit in submits.items():
        if gen in starts:
            stages['executor_wait'].append(starts[gen]['time_us']-submit['time_us'])
        if gen not in ends and gen not in replacements:
            failures.append(f'generation {gen} neither completed nor replaced')
        if gen in ends and ends[gen]['a'] != 0:
            failures.append(f'generation {gen} Driver error {ends[gen]["a"]}')
    for row in kinds['SUBMIT']:
        if row['b'] != 0:
            failures.append(f'submit failed for canvas version {row["version"]}')
    for point, event in by_point.items():
        sample = event.get('SAMPLE', [None])[0]
        push = event.get('PUSH', [None])[0]
        if sample is None or push is None:
            failures.append(f'point {point} missing sample/push')
            continue
        if push['b'] < 0:
            failures.append(f'point {point} FIFO write failed ({push["b"]})')
            continue
        stages['sample_to_push'].append(push['time_us']-sample['time_us'])
        pop = event.get('POP', [None])[0]
        dispatch = event.get('DISPATCH', [None])[0]
        accepted = event.get('EVENT', [None])[0]
        if pop is None or dispatch is None or accepted is None:
            failures.append(f'point {point} not delivered to Sketch')
            continue
        stages['fifo_wait'].append(pop['time_us']-push['time_us'])
        canvas = event.get('CANVAS', [None])[0]
        if canvas:
            stages['point_to_canvas'].append(canvas['time_us']-sample['time_us'])
            next_render = next((r for r in kinds['RENDER']
                if r['time_us'] >= canvas['time_us'] and
                   r['version'] >= canvas['version']), None)
            if next_render:
                stages['canvas_to_render'].append(next_render['time_us']-canvas['time_us'])
        elif push['a'] == MOVE:
            # A MOVE can be outside Canvas or duplicate; keep it classified.
            pass
        if push['a'] in (PRESS, RELEASE):
            matching = [ends[gen] for gen, frame in submits.items()
                if frame['time_us'] >= accepted['time_us'] and
                   frame['version'] >= accepted['version'] and
                   gen in ends and ends[gen]['a'] == 0 and
                   ends[gen]['b'] == 1 and gen not in replacements]
            if not matching and push['a'] == RELEASE:
                failures.append(f'release point {point} has no completed final frame')
            elif matching:
                key = 'press_to_done' if push['a'] == PRESS else 'release_to_done'
                stages[key].append(min(r['time_us'] for r in matching)-sample['time_us'])
    versions = [r['version'] for r in kinds['CANVAS']]
    if versions and any(b <= a for a, b in zip(versions, versions[1:])):
        failures.append('Canvas version not increasing')
    max_version = max(versions, default=0)
    completed_versions = [submits[g]['version'] for g, end in ends.items()
                          if g in submits and end['a'] == 0 and
                          end['b'] == 1 and g not in replacements]
    if max_version and max(completed_versions, default=0) < max_version:
        failures.append('final Canvas version not physically completed')
    if meta['overflow']:
        failures.append(f'journal overflow {meta["overflow"]}; missing events not inferred')
    if not kinds['SAMPLE'] or not kinds['SUBMIT']:
        failures.append('no stroke or display submission captured')
    return dict(valid=not failures, failures=failures,
                meta=meta, samples=len(samples),
                read_failures=read_failures,
                fifo_failures=sum(r['b'] < 0 for r in kinds['PUSH']),
                fifo_high_watermark=max((r['b'] for r in kinds['PUSH']), default=0),
                frame_replacements=len(kinds['REPLACE']),
                ssd_breakdown_available=any(r['a'] > 0 for r in kinds['SSD_SPI']),
                final_canvas_version=max_version,
                completed_canvas_version=max(completed_versions, default=0),
                stage_us={key: distribution(value) for key, value in stages.items()},
                caveat='Driver completion is not optical appearance; FT6336 sampling between polls is unknowable')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('trace', type=Path)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    meta, rows = parse(args.trace.read_text(encoding='utf-8').splitlines())
    result = analyze(meta, rows)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print('PASS' if result['valid'] else 'INCOMPLETE', result['failures'])


if __name__ == '__main__':
    main()
