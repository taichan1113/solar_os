"""Offline Writer contrast timeline. Never opens a serial port or changes firmware.

NumPy accelerates the existing bilinear sampler/template registration; Pillow
and FFmpeg retain decoded resolution/PTS. Matplotlib exports standalone plots.
New-character cells (outside the immutable target) provide optical sync. A fitted
visible offset is NOT a calibration of BUSY/optical/camera latency.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import queue
import re
import statistics
import subprocess
import threading

import analyze_writer_quality as journal
import measure_writer_video as video


def median(values):
    return statistics.median(values) if values else None


def immutable_geometry(masks, following):
    """Validate journal masks; synchronization uses geometry, never target density."""
    black = {name: sorted(video.optical.black_coordinates(mask)) for name, mask in masks.items()}
    if set(black) != {'first', 'group', 'fixed'}:
        raise ValueError('first/group/independent fixed masks required')
    x, y, w, h = masks['group']['roi']
    cell = masks['first']['roi'][2]
    sync = [(x + w + i * cell + 1, y, cell - 2, h) for i in range(following)]
    for a, b, c, d in sync:
        if not (0 <= a < a + c <= 800 and 0 <= b < b + d <= 480):
            raise ValueError('synchronization cell outside panel')
        for mask in masks.values():
            e, f, g, k = mask['roi']
            if a < e + g and e < a + c and b < f + k and f < b + d:
                raise ValueError('synchronization ROI overlaps a measurement ROI')
    return black, sync


class ImageMetrics:
    """Vectorized equivalent of the prior unscaled bilinear optical sampler."""
    def __init__(self, masks, corners, sync_cells=(), crop=(0, 0, 800, 480)):
        import numpy as np
        self.np = np
        self.masks, self.crop = masks, crop
        self.coeff = video.optical.homography(corners)
        self.black, self.white = {}, {}
        for name, mask in masks.items():
            pixels = video.optical.black_coordinates(mask)
            x, y, w, h = mask['roi']
            near = {(a + dx, b + dy) for a, b in pixels for dx in (-1, 0, 1) for dy in (-1, 0, 1)}
            white = {(a, b) for a in range(x, x + w) for b in range(y, y + h)} - near
            if len(white) < 10:
                raise ValueError('insufficient white mask')
            self.black[name] = np.asarray(sorted(pixels), dtype=float) + .5
            self.white[name] = np.asarray(sorted(white), dtype=float) + .5
        ref = video.optical.black_coordinates(masks['fixed'])
        x, y, w, h = masks['fixed']['roi']
        neighbor = {(a + dx, b + dy) for a, b in ref for dx in (-1, 0, 1) for dy in (-1, 0, 1)}
        white = (neighbor - ref) & {(a, b) for a in range(x, x + w) for b in range(y, y + h)}
        self.register_white = np.asarray(sorted(white), dtype=float) + .5
        self.sync = []
        for x, y, w, h in sync_cells:
            target = np.asarray([(a, b) for a in range(x, x + w) for b in range(y, y + h)], dtype=float) + .5
            white = np.asarray([(a, b) for a in range(x, x + w) for b in (y - 3, y - 2, y + h + 2, y + h + 3)], dtype=float) + .5
            self.sync.append((target, white))

    def sample(self, image, coords, offsets):
        np = self.np
        xy = coords[None, :, :] + np.asarray(offsets, dtype=float)[:, None, :]
        x, y = xy[..., 0], xy[..., 1]
        a, b, c, d, e, f, g, h = self.coeff
        den = g * x + h * y + 1
        u, v = (a * x + b * y + c) / den - self.crop[0], (d * x + e * y + f) / den - self.crop[1]
        ix, iy = np.floor(u).astype(int), np.floor(v).astype(int)
        if np.any(ix < 0) or np.any(iy < 0) or np.any(ix >= image.shape[1] - 1) or np.any(iy >= image.shape[0] - 1):
            raise ValueError('ROI sampling exceeds original crop')
        dx, dy = u - ix, v - iy
        return ((1 - dx) * (1 - dy) * image[iy, ix] + dx * (1 - dy) * image[iy, ix + 1] +
                (1 - dx) * dy * image[iy + 1, ix] + dx * dy * image[iy + 1, ix + 1])

    def register(self, image, bound=4):
        np = self.np
        def choose(offsets):
            B = self.sample(image, self.black['fixed'], offsets)
            W = self.sample(image, self.register_white, offsets)
            all_values = np.concatenate((B, W), axis=1)
            spread = all_values.std(axis=1)
            score = np.divide(W.mean(axis=1) - B.mean(axis=1), spread,
                              out=np.full(len(spread), -np.inf), where=spread > 0)
            return max((float(v), *o) for v, o in zip(score, offsets))
        _, x, y = choose([(i / 2, j / 2) for i in range(-8, 9) for j in range(-8, 9)])
        score, x, y = choose([(x + i / 8, y + j / 8) for i in range(-4, 5) for j in range(-4, 5)
                              if abs(x + i / 8) <= bound and abs(y + j / 8) <= bound])
        return dict(dx=x, dy=y, normalized_template_score=score,
                    boundary_hit=abs(x) == bound or abs(y) == bound, search_bound=bound)

    def measure(self, image):
        np = self.np
        pose = self.register(image)
        if pose['boundary_hit'] or pose['normalized_template_score'] < 1:
            raise ValueError('independent reference registration failed/boundary/weak template')
        offsets = [(pose['dx'] + dx, pose['dy'] + dy) for dx in (-.25, 0, .25) for dy in (-.25, 0, .25)]
        regions = {}
        for name in self.masks:
            black, white = self.sample(image, self.black[name], offsets), self.sample(image, self.white[name], offsets)
            B, W = np.median(black, axis=1), np.median(white, axis=1)
            if np.any(W <= 0):
                raise ValueError('non-positive white background')
            C = (W - B) / W
            regions[name] = dict(B=float(B[4]), W=float(W[4]), C=float(C[4]),
                black_MAD=float(np.median(np.abs(black[4] - B[4]))),
                white_MAD=float(np.median(np.abs(white[4] - W[4]))),
                C_shift_min=float(C.min()), C_shift_max=float(C.max()),
                black_pixels=len(self.black[name]))
        signals = []
        for target, white in self.sync:
            B = self.sample(image, target, [(pose['dx'], pose['dy'])]).mean()
            W = self.sample(image, white, [(pose['dx'], pose['dy'])]).mean()
            signals.append(float((W - B) / W) if W > 0 else None)
        return regions, pose, signals


def decode_selected(source, executable, crop, dense, post_end, frozen_pts=(), sparse_s=5, post_s=1):
    """One streaming decode; source frame numbers and PTS come from showinfo.

    Crop is a lossless spatial selection, no scaling/fps synthesis. The ALL
    showinfo precedes select, so frame_index indexes the original decode order.
    """
    from PIL import Image
    x, y, w, h = crop
    terms = [f'between(t,{dense[0]:.9f},{dense[1]:.9f})',
             f'isnan(prev_selected_t)+gte(t-prev_selected_t,{sparse_s})',
             f'gte(t,{post_end:.9f})*gte(t-prev_selected_t,{post_s})']
    terms += [f'between(t,{v - 1e-5:.9f},{v + 1e-5:.9f})' for v in frozen_pts]
    filt = "showinfo@all,select='" + '+'.join(terms) + f"',crop={w}:{h}:{x}:{y},format=rgb24,showinfo@selected"
    proc = subprocess.Popen([executable, '-hide_banner', '-nostdin', '-i', str(source), '-map', '0:v:0',
        '-vf', filt, '-an', '-fps_mode', 'vfr', '-f', 'rawvideo', 'pipe:1'], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    messages, errors = queue.Queue(), []
    def reader():
        source_indices = {}
        for raw in iter(proc.stderr.readline, b''):
            line = raw.decode('utf-8', 'replace')
            match = re.search(r'n:\s*(\d+).*?pts_time:([-\d.eE+]+)', line)
            if match and 'showinfo@all' in line:
                source_indices[float(match[2])] = int(match[1])
            elif match and 'showinfo@selected' in line:
                pts = float(match[2])
                messages.put((source_indices.get(pts), pts))
            errors.append(line)
            if len(errors) > 40:
                del errors[0]
        messages.put(None)
    thread = threading.Thread(target=reader, daemon=True)
    thread.start()
    try:
        size = w * h * 3
        while True:
            payload = bytearray()
            while len(payload) < size:
                piece = proc.stdout.read(size - len(payload))
                if not piece:
                    break
                payload.extend(piece)
            if not payload:
                break
            if len(payload) != size:
                raise ValueError('partial decoded original frame')
            meta = messages.get(timeout=30)
            if meta is None or meta[0] is None:
                raise ValueError('selected frame missing original index/PTS')
            yield meta[0], meta[1], Image.frombytes('RGB', (w, h), bytes(payload))
        thread.join(timeout=30)
        if proc.wait(timeout=30):
            raise ValueError('FFmpeg failed: ' + ''.join(errors)[-3000:])
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()
        proc.stdout.close()
        proc.stderr.close()


def detect_appearance(rows, signal_index, pre_window, final_window, search_window):
    """Find a persistent independent-cell darkening, not a target fade time."""
    valid = [(r['pts'], r['sync_signals'][signal_index]) for r in rows
             if r['valid'] and r.get('sync_signals') and r['sync_signals'][signal_index] is not None]
    pre = [v for t, v in valid if pre_window[0] <= t <= pre_window[1]]
    final = [v for t, v in valid if final_window[0] <= t <= final_window[1]]
    if len(pre) < 3 or len(final) < 3:
        return dict(valid=False, reason='appearance reference frames missing')
    low, high = median(pre), median(final)
    noise = median([abs(v - low) for v in pre])
    if high - low < max(.02, 8 * noise):
        return dict(valid=False, reason='new-character contrast not separated from noise')
    levels, candidates = {}, []
    values = [(t, v) for t, v in valid if search_window[0] <= t <= search_window[1]]
    for fraction in (.1, .5, .9):
        threshold = low + fraction * (high - low)
        crossings = [(values[i - 1][0], t) for i, (t, v) in enumerate(values[1:], 1)
                     if values[i - 1][1] < threshold <= v and i + 1 < len(values)
                     and values[i + 1][1] >= threshold and t - values[i - 1][0] <= .1]
        # Small compression ripples can cause repeated crossings during one
        # transition. Widely separated crossings are ambiguous and rejected.
        clusters = []
        for pair in crossings:
            if not clusters or pair[0] - clusters[-1][-1][1] > .15:
                clusters.append([])
            clusters[-1].append(pair)
        if len(clusters) != 1:
            return dict(valid=False, reason=f'{fraction:g} appearance has {len(clusters)} candidate transitions')
        pair = clusters[0][0]
        levels[str(fraction)] = dict(before_pts=pair[0], after_pts=pair[1], midpoint=sum(pair) / 2)
        if fraction == .5:
            candidates = pair
    return dict(valid=True, pre=low, final=high, baseline_MAD=noise, thresholds=levels,
                video_pts=sum(candidates) / 2, bracket_s=candidates[1] - candidates[0],
                transition_warning=(levels['0.5']['midpoint'] - levels['0.1']['before_pts'] > .15
                                    or levels['0.9']['after_pts'] - levels['0.5']['midpoint'] > .15),
                caveat='threshold uses final visible glyph contrast; appearance is not physical completion')


def fit_offset(appearances, physical, t0_us):
    good = [(a, p) for a, p in zip(appearances, physical) if a.get('valid')]
    if len(good) < 3:
        return dict(valid=False, reason='fewer than 3 independent-cell events identified')
    times = [a['video_pts'] for a, _ in good]
    if any(a >= b for a, b in zip(times, times[1:])):
        return dict(valid=False, reason='appearance events not uniquely ordered')
    offsets = [a['video_pts'] - (p['end'] - t0_us) / 1e6 for a, p in good]
    center = median(offsets)
    residuals = [v - center for v in offsets]
    bracket = max(a['bracket_s'] for a, _ in good)
    scatter = max(abs(v) for v in residuals)
    # This is an empirical correspondence envelope, NOT a bound on hidden
    # camera latency or exact electrical/optical completion.
    acceptable = scatter <= .075
    return dict(valid=acceptable, reason=None if acceptable else 'common offset residual exceeds 75ms',
        video_origin_s=center, video_per_device_second=1, event_count=len(good),
        residuals_s=residuals, offset_range_s=[min(offsets), max(offsets)],
        residual_max_s=scatter, residual_MAD_s=median([abs(x) for x in residuals]),
        max_appearance_bracket_s=bracket, empirical_envelope_s=scatter + bracket / 2,
        absolute_latency_bound_s=None,
        caveat='common visible offset absorbs optical/camera delay; unit clock scale fixed, not calibrated')


def burst_sync_indices(physical, formation=20, total=29):
    """One independently appearing NEW glyph per completed snapshot.

    Replacement/coalescing can make several letters appear in the same update.
    Select the last new letter in each physical snapshot, never zip nine input
    letters onto an unrelated number of physical updates.
    """
    if not physical or physical[0].get('chars')!=formation or physical[-1].get('chars')!=total:
        raise ValueError('burst formation/final snapshot mismatch')
    counts=[r['chars'] for r in physical]
    if counts!=sorted(set(counts)):
        raise ValueError('non-monotonic burst physical membership')
    return [n-formation-1 for n in counts[1:]]


def event_intervals(physical, mapping, t0_us, appearances):
    if not mapping.get('valid'):
        return []
    origin = mapping['video_origin_s']
    events = []
    for i, p in enumerate(physical):
        row = dict(index=i + 1, gen=p['gen'], driver_start_us=p['start'], driver_end_us=p['end'],
            busy_duration_us=p.get('busy_us'),
            video_start=origin + (p['start'] - t0_us) / 1e6,
            video_end=origin + (p['end'] - t0_us) / 1e6)
        a = appearances[i] if i < len(appearances) else None
        if a and a.get('valid'):
            row['visible_low'] = a['thresholds']['0.1']['before_pts']
            row['visible_high'] = a['thresholds']['0.9']['after_pts']
        else:
            row['visible_low'], row['visible_high'] = row['video_start'], row['video_end']
        events.append(row)
    return events


def stable_gaps(events, formed, last_pts, margin):
    """Exclude inferred driver operation AND independently visible transitions."""
    occupied = [(min(e['video_start'], e['visible_low']) - margin,
                 max(e['video_end'], e['visible_high']) + margin) for e in events]
    gaps = []
    left = formed + .5
    for i, (start, end) in enumerate(occupied):
        gaps.append((left, start, i))
        left = max(left, end)
    gaps.append((left, last_pts, len(events)))
    return gaps


def summarize_window(rows, low, high, regions=('group', 'first', 'fixed'), minimum=2):
    selected = [r for r in rows if r['valid'] and low <= r['pts'] <= high]
    if high <= low or len(selected) < minimum:
        return dict(measurable=False, reason='insufficient stable original frames', frames=len(selected), interval=[low, high])
    result = dict(measurable=True, frames=len(selected), interval=[low, high],
                  actual_pts=[r['pts'] for r in selected], regions={})
    for name in regions:
        metrics = [r['regions'][name] for r in selected]
        C = [r['C'] for r in metrics]
        W = [r['W'] for r in metrics]
        result['regions'][name] = dict(C=median(C), C_min=min(C), C_max=max(C),
            W=median(W), C_MAD=median([abs(v - median(C)) for v in C]),
            C_shift_min=min(r['C_shift_min'] for r in metrics),
            C_shift_max=max(r['C_shift_max'] for r in metrics))
    return result


def delta_event(before, after):
    if not before['measurable'] or not after['measurable']:
        return dict(measurable=False, reason='no separable stable window on both sides', before=before, after=after)
    changes = {}
    for name in before['regions']:
        b, a = before['regions'][name], after['regions'][name]
        changes[name] = dict(delta_C=a['C'] - b['C'],
            delta_shift_envelope=[a['C_shift_min'] - b['C_shift_max'], a['C_shift_max'] - b['C_shift_min']],
            white_relative_change=a['W'] / b['W'] - 1)
    return dict(measurable=True, before=before, after=after, regions=changes,
                caveat='within one correlated trial; shift envelopes are not confidence intervals')


def event_measurements(rows, events, gaps, window_s=.2):
    """Immediate stable sides only; never average a long late recovery into ΔC."""
    results = []
    for i, event in enumerate(events):
        a, b, _ = gaps[i]
        c, d, _ = gaps[i + 1]
        before = summarize_window(rows, max(a, b - window_s), b)
        after = summarize_window(rows, c, min(d, c + window_s))
        results.append(dict(**event, **delta_event(before, after)))
    return results


def no_update_rate(rows, low, high, name='group'):
    selected = [r for r in rows if r['valid'] and low <= r['pts'] <= high]
    if len(selected) < 3 or selected[-1]['pts'] - selected[0]['pts'] < .08:
        return dict(measurable=False, reason='too few/dense frames or short interval')
    values = [(r['pts'], r['regions'][name]['C']) for r in selected]
    slopes = [(b[1] - a[1]) / (b[0] - a[0]) for i, a in enumerate(values)
              for b in values[i + 1:] if b[0] - a[0] >= .05]
    if not slopes:
        return dict(measurable=False, reason='no non-duplicate time span')
    edge = min(3, len(values) // 2)
    whites = [r['regions'][name]['W'] for r in selected]
    return dict(measurable=True, frames=len(values), first_pts=values[0][0], last_pts=values[-1][0],
        duration_s=values[-1][0] - values[0][0], descriptive_C_per_s=median(slopes),
        edge_delta_C=median([v for _, v in values[-edge:]]) - median([v for _, v in values[:edge]]),
        white_min=min(whites), white_max=max(whites),
        white_edge_relative_change=median(whites[-edge:]) / median(whites[:edge]) - 1,
        caveat='descriptive Theil-Sen slope; correlated frames, no p-value or independent sample count')


def phase_for(pts, events, margin):
    touched = [e for e in events if min(e['video_start'], e.get('visible_low', e['video_start'])) - margin
               <= pts <= max(e['video_end'], e.get('visible_high', e['video_end'])) + margin]
    if touched:
        if len(touched) == 1 and touched[0]['video_start'] <= pts <= touched[0]['video_end']:
            return 'during_inferred_driver', touched[0]['gen']
        return 'uncertain', touched[0]['gen'] if len(touched) == 1 else None
    if not events:
        return 'unknown', None
    if pts < events[0]['video_start']:
        return 'before_following', None
    prior = [e for e in events if e['video_end'] < pts]
    return ('no_update_after_final' if len(prior) == len(events) else 'between_updates'), prior[-1]['gen'] if prior else None


def fade_onset(rows, before, search_low, search_high, name='group'):
    """Describe target change AFTER independent synchronization; never fit to it."""
    if not before['measurable']:
        return dict(measurable=False, reason='no stable before window')
    b = before['regions'][name]
    drop = max(.03 * b['C'], 6 * b['C_MAD'])
    selected = [r for r in rows if r['valid'] and search_low <= r['pts'] <= search_high]
    for i in range(1, len(selected) - 2):
        if (selected[i - 1]['regions'][name]['C'] >= b['C'] - drop
            and all(r['regions'][name]['C'] < b['C'] - drop for r in selected[i:i + 3])):
            return dict(measurable=True, threshold_drop_C=drop, baseline_C=b['C'],
                        pts_bracket=[selected[i - 1]['pts'], selected[i]['pts']],
                        caveat='first persistent >3% or 6-MAD drop; descriptive camera observation, not drive onset')
    return dict(measurable=False, reason='no persistent threshold crossing')


def guard_sensitivity(rows, events, formed, last_pts, envelope):
    """Explicit uncalibrated-latency assumptions, never select by target outcome."""
    results = []
    for systematic in (0, .05, .10):
        margin = .05 + systematic + envelope
        gaps = stable_gaps(events, formed, last_pts, margin)
        deltas = event_measurements(rows, events, gaps)
        results.append(dict(assumed_systematic_s=systematic, margin_s=margin,
                            gaps=gaps, events=deltas,
                            caveat='conditional sensitivity only; no measured bound on absolute latency'))
    return results


def analyze_experiment(config, out, executable):
    import numpy as np
    from PIL import Image, ImageDraw
    source = Path(config['video'])
    if not source.is_file():
        raise ValueError('original video missing: ' + str(source))
    analysis = journal.analyze(Path(config['log']).read_text(encoding='utf-8'))
    burst=analysis.get('protocol')=='writer-burst'
    if not analysis['valid'] or analysis.get('protocol') not in ('writer-video','writer-burst'):
        raise ValueError('complete valid Writer video journal required')
    if burst and (not config.get('camera_pose_id') or 'crop' not in config):
        raise ValueError('burst requires newly calibrated camera pose ID and native crop')
    previous = json.loads(Path(config['previous']).read_text(encoding='utf-8'))
    corners = json.loads(Path(config['corners']).read_text(encoding='utf-8'))['T0']
    masks = analysis['black_masks']
    _, cells = immutable_geometry(masks, 9 if burst else analysis['first_following_updates'])
    hash_before = hashlib.sha256()
    with source.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            hash_before.update(block)
    if hash_before.hexdigest() != previous['source_sha256']:
        raise ValueError('original video SHA differs from prior measurement')
    initial_mapping = previous['synchronization']
    physical = analysis['rows']['physical']
    t0_us = physical[0]['end']
    initial = initial_mapping['video_origin_s'] + t0_us / 1e6
    final = initial_mapping['video_origin_s'] + physical[-1]['end'] / 1e6
    out.mkdir()
    (out / 'frames').mkdir()
    crop = tuple(config.get('crop',(550, 350, 1000, 180)))  # native pixels, no resizing
    if len(crop)!=4 or any(not isinstance(n,int) for n in crop) or min(crop)<0 or min(crop[2:])<1:
        raise ValueError('invalid native crop')
    engine = ImageMetrics(masks, corners, cells, crop)
    frozen = [f['actual_video_time_s'] for f in previous['frames']]
    rows, baseline = [], {name: previous['regions'][name]['T0']['C'] for name in masks}
    dense = (max(0, initial - 1), final + 2)
    for index, pts, image in decode_selected(source, executable, crop, dense, final + 2, frozen):
        row = dict(frame_index=index, pts=pts, valid=False, reason=None, regions={}, sync_signals=[])
        if pts < dense[0]:
            row['reason'] = 'before target formation; sparse overview frame only'
        else:
            try:
                metrics, pose, signals = engine.measure(np.asarray(image.convert('L'), dtype=float))
                row.update(valid=True, regions=metrics, registration=pose, sync_signals=signals)
                for name, values in metrics.items():
                    values['R'] = values['C'] / baseline[name]
            except ValueError as exc:
                row['reason'] = str(exc)
        rows.append(row)
        # Native crop evidence, deterministic selection independent of density.
        if dense[0] <= pts <= dense[1]:
            path = out / 'frames' / f'{index:06d}.png'
            image.save(path)
            row['image'] = str(path.name)
        if len(rows) % 100 == 0:
            print(config['id'], 'decoded/measured', len(rows), 'PTS', pts, flush=True)
    if not rows:
        raise ValueError('no decoded frames')
    appearances = [detect_appearance(rows, i, (initial + .5, initial + 1.5),
                    (final + .5, final + 1.5), (initial + 1.5, final + .5)) for i in range(len(cells))]
    cell_indices=burst_sync_indices(physical) if burst else list(range(len(cells)))
    cells=[cells[i] for i in cell_indices]
    appearances=[appearances[i] for i in cell_indices]
    mapping = fit_offset(appearances, physical[1:], t0_us)
    events = event_intervals(physical[1:], mapping, t0_us, appearances)
    margin = .05 + .10 + mapping.get('empirical_envelope_s', 0)
    origin = mapping.get('video_origin_s', initial)
    gaps = stable_gaps(events, origin, rows[-1]['pts'], margin) if events else []
    windows = [summarize_window(rows, a, b) for a, b, _ in gaps]
    deltas = event_measurements(rows, events, gaps)
    burst = delta_event(windows[0], summarize_window(rows, final + .5, final + 1.5)) if windows else dict(measurable=False)
    rates = [dict(after_updates=count, interval=[a, b],
             regions={name: no_update_rate(rows, a, b, name) for name in masks}) for a, b, count in gaps]
    post = {label: dict(interval=[a, b], regions={name: no_update_rate(rows, a, b, name) for name in masks})
            for label, a, b in (('early_settled', events[-1]['video_end'] + .5, events[-1]['video_end'] + 2),
                               ('late', events[-1]['video_end'] + 2, rows[-1]['pts']))} if events else {}
    sensitivity = guard_sensitivity(rows, events, origin, rows[-1]['pts'],
                                    mapping.get('empirical_envelope_s', 0)) if events else []
    onset = {name: fade_onset(rows, windows[0], origin + 1.5, final + .5, name)
             for name in masks} if windows else {}
    for entry in onset.values():
        if entry['measurable']:
            entry['from_first_inferred_start_s'] = [t - events[0]['video_start'] for t in entry['pts_bracket']]
    reconciliation = []
    for old in previous['frames']:
        match = [r for r in rows if abs(r['pts'] - old['actual_video_time_s']) < 1e-5 and r['valid']]
        reconciliation.append(dict(point=old['point'], pts=old['actual_video_time_s'], matched=len(match),
            delta_C={name: match[0]['regions'][name]['C'] - old['regions'][name]['C'] for name in masks} if len(match) == 1 else None))
    # All frames get explicit uncertainty status; none during inferred operation
    # or appearance/guard intervals are used by stable-window estimators.
    for row in rows:
        row['relative_s'] = row['pts'] - origin
        row['phase'], row['gen'] = phase_for(row['pts'], events, margin)
        row['stable'] = any(a <= row['pts'] <= b for a, b, _ in gaps) and row['valid']
        if burst:
            row['input_relative_s']=row['pts']-(origin+(analysis['rows']['input'][20]['sent']-t0_us)/1e6)
    result = dict(experiment=config['id'], source=str(source), source_sha256=hash_before.hexdigest(),
        physical_updates=len(physical), following=len(physical) - 1,
        initial_formation=physical[0],
        interval_us=analysis['mean_burst_update_interval_us'], masks=masks, sync_cells=cells,
        source_resolution=previous['video']['decoded_size'], crop=crop,
        sampling=dict(dense_range=dense, sparse_s=5, post_final_s=1, frozen_prior_pts=frozen,
                      actual_decoded_index_and_pts=True, no_spatial_resize=True),
        synchronization=mapping, appearances=appearances, events=deltas,
        stable_margin_s=margin, latency_sensitivity_s=.10, settling_guard_s=.05, immediate_delta_window_s=.2,
        no_update=rates, burst=burst, reconciliation=reconciliation, baseline=baseline,
        guard_sensitivity=sensitivity, fade_onset=onset, post_update_segments=post,
        previous_R={name: previous['regions'][name]['R'] for name in masks},
        frame_count=len(rows), valid_frame_count=sum(r['valid'] for r in rows),
        policy=analysis.get('policy'),response=analysis.get('response'),
        camera_pose_id=config.get('camera_pose_id'),sync_cell_indices=cell_indices,
        correlated_frames=True, independent_trials=1,
        caveat='Stable classification assumes stated sensitivity margins; absolute optical/camera latency unbounded.',
        frames=rows)
    (out / 'result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    with (out / 'timeline.csv').open('w', newline='', encoding='utf-8') as stream:
        fields = ['experiment', 'frame_index', 'pts', 'relative_s', 'gen', 'phase', 'stable',
                  'roi', 'B', 'W', 'C', 'R', 'white_MAD', 'black_MAD', 'C_shift_min', 'C_shift_max',
                  'dx', 'dy', 'valid', 'reason']
        if burst:fields.append('input_relative_s')
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            for name in masks:
                writer.writerow(dict(experiment=config['id'], **{k: row.get(k) for k in fields if k in row},
                    roi=name, **{k: v for k, v in row['regions'].get(name, {}).items() if k in fields},
                    dx=row.get('registration', {}).get('dx'), dy=row.get('registration', {}).get('dy')))
    # The contact sheet shows the independent new-character appearance brackets.
    sheet = Image.new('RGB', (1000, len(appearances) * 210), 'white')
    draw = ImageDraw.Draw(sheet)
    for i, a in enumerate(appearances):
        if not a.get('valid'):
            draw.text((5, i * 210 + 5), f'event {i+1}: unavailable: {a.get("reason")}', fill='black')
            continue
        bracket = a['thresholds']['0.5']
        for column, key in enumerate(('before_pts', 'after_pts')):
            match = min(rows, key=lambda r: abs(r['pts'] - bracket[key]))
            with Image.open(out / 'frames' / match['image']) as frame:
                sheet.paste(frame.resize((500, 90)), (column * 500, i * 210 + 25))
            draw.text((column * 500 + 5, i * 210 + 5), f'{key} PTS={match["pts"]:.6f} n={match["frame_index"]}', fill='black')
        draw.text((5, i * 210 + 125), f'event {i+1} gen={physical[i+1]["gen"]}; new-letter ROI={cells[i]}', fill='black')
    sheet.save(out / 'sync-contact-sheet.png')
    print(config['id'], 'sync', mapping, flush=True)
    return result


def plot_results(results, out):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    colors = {'A1': '#1768ac', 'A2': '#b33d28', 'B2': '#268447', 'C0': '#1768ac', 'C1': '#268447'}
    fig, axes = plt.subplots(len(results), 1, figsize=(11, max(4,len(results)*3.7)), constrained_layout=True,squeeze=False)
    for ax, result in zip(axes[:,0], results):
        rows = [r for r in result['frames'] if r['valid'] and -.2 <= r['relative_s'] <= 7]
        for name, color in (('group', colors[result['experiment']]), ('fixed', '#666666')):
            ax.plot([r['relative_s'] for r in rows], [r['regions'][name]['R'] for r in rows], '.-',
                    color=color, markersize=2, linewidth=.8, label='20-a group' if name == 'group' else 'OTP reference')
        origin = result['synchronization'].get('video_origin_s', 0)
        for event in result['events']:
            ax.axvspan(event['video_start'] - origin, event['video_end'] - origin, color='orange', alpha=.22)
            ax.axvline(event['video_end'] - origin, color='#a46b00', linewidth=.6)
        for a in result['appearances']:
            if a.get('valid'):
                ax.plot(a['video_pts'] - origin, .98, marker='v', color='black', markersize=4)
        white_ax = ax.twinx()
        white0 = result['burst']['before']['regions']['group']['W'] if result['burst']['measurable'] else 1
        white_ax.plot([r['relative_s'] for r in rows],
                      [100 * (r['regions']['group']['W'] / white0 - 1) for r in rows],
                      ':', color='#8599ac', linewidth=.8)
        white_ax.set(ylabel='White W change (%)', ylim=(-4, 4))
        ax.set(title=f'{result["experiment"]}: {result["following"]} following updates, {result["interval_us"]/1000:.1f} ms',
               xlabel='Seconds from fitted optical offset (not calibrated BUSY time)', ylabel='R = C / frozen T0 C')
        ax.grid(alpha=.2); ax.legend(loc='lower left')
    fig.suptitle('Existing Writer text: timeline; orange = inferred driver operation; triangles = new glyphs')
    fig.savefig(out / '01-timeline.png', dpi=180); plt.close(fig)
    fig, ax = plt.subplots(figsize=(9, 5), constrained_layout=True)
    for result in results:
        xs, ys = [], []
        for i, event in enumerate(result['events']):
            if event['measurable']:
                xs.append(i + 1); ys.append(event['after']['regions']['group']['C'] / result['baseline']['group'])
        if xs:
            ax.scatter(xs, ys, label=result['experiment'] + ' stable post-event', color=colors[result['experiment']])
        ax.scatter([0], [1], color=colors[result['experiment']], marker='s', s=30)
        conditional = result['guard_sensitivity'][1]['events'] if result['guard_sensitivity'] else []
        measured = [e for e in conditional if e['measurable']]
        if measured:
            ax.scatter([e['index'] for e in measured],
                       [e['after']['regions']['group']['C'] / result['baseline']['group'] for e in measured],
                       edgecolors=colors[result['experiment']], facecolors='none', marker='o', s=60,
                       label=result['experiment'] + ' conditional: assumed extra latency <=50ms')
        ax.scatter([result['following']], [result['previous_R']['group']], color=colors[result['experiment']],
                   marker='x', s=90, label=result['experiment'] + ' frozen T1')
    ax.set(xlabel='Following physical update count', ylabel='Group R', title='No interpolation: square = T0; x = prior T1; hollow = conditional immediate window')
    ax.legend(); ax.grid(alpha=.2); fig.savefig(out / '02-update-count.png', dpi=180); plt.close(fig)
    fig, ax = plt.subplots(figsize=(9, 5), constrained_layout=True)
    for result in results:
        if not result['events']:
            continue
        end = result['events'][-1]['video_end']
        rows = [r for r in result['frames'] if r['valid'] and r['pts'] >= end + .5]
        ax.plot([r['pts'] - end for r in rows], [r['regions']['group']['R'] for r in rows], '.-',
                color=colors[result['experiment']], label=result['experiment'], linewidth=.8, markersize=2)
    ax.set(xlabel='Seconds since inferred final completion', ylabel='Group R', title='Recorded post-update interval: no additional trial updates')
    ax.legend(); ax.grid(alpha=.2); fig.savefig(out / '03-no-update.png', dpi=180); plt.close(fig)
    fig, axes = plt.subplots(2, 1, figsize=(10, 8), constrained_layout=True)
    ax = axes[0]
    for result in results:
        for i, event in enumerate(result['events']):
            if event['measurable']:
                ax.scatter(f'{result["experiment"]}-{i+1}', event['regions']['group']['delta_C'], color=colors[result['experiment']])
        if result['burst']['measurable']:
            measure = result['burst']['regions']['group']
            delta = measure['delta_C']; lo, hi = measure['delta_shift_envelope']
            ax.errorbar(result['experiment'] + '-whole burst', delta,
                        yerr=[[delta - lo], [hi - delta]], color=colors[result['experiment']],
                        marker='s', capsize=4, linestyle='none')
    ax.axhline(0, color='black', linewidth=.7)
    ax.set(ylabel='C_after - C_before', title='Primary: 100ms systematic allowance; unmeasurable individual events omitted')
    ax.tick_params(axis='x', labelrotation=30); ax.grid(axis='y', alpha=.2)
    ax = axes[1]
    for result in results:
        for event in (result['guard_sensitivity'][1]['events'] if result['guard_sensitivity'] else []):
            if event['measurable']:
                measure = event['regions']['group']
                delta = measure['delta_C']; lo, hi = measure['delta_shift_envelope']
                ax.errorbar(f'{result["experiment"]}-{event["index"]}', delta,
                            yerr=[[delta - lo], [hi - delta]], color=colors[result['experiment']],
                            marker='o', markerfacecolor='none', capsize=4, linestyle='none')
    ax.axhline(0, color='black', linewidth=.7)
    ax.set(ylabel='C_after - C_before', title='Sensitivity only: assume extra systematic latency <=50ms (uncalibrated)')
    fig.suptitle('Bars = +/-0.25 logical pixel shift envelope, NOT confidence intervals')
    ax.tick_params(axis='x', labelrotation=30); ax.grid(axis='y', alpha=.2)
    fig.savefig(out / '04-event-deltas.png', dpi=180); plt.close(fig)
    for result in results:
        fig, axes = plt.subplots(len(result['appearances']), 1, figsize=(10, len(result['appearances']) * 1.6),
                                 constrained_layout=True, squeeze=False)
        for i, appearance in enumerate(result['appearances']):
            ax = axes[i, 0]
            if not appearance['valid']:
                ax.set_title(appearance['reason']); continue
            t = appearance['video_pts']
            rows = [r for r in result['frames'] if r['valid'] and t - .65 <= r['pts'] <= t + .3]
            cell=result.get('sync_cell_indices',list(range(len(result['appearances']))))[i]
            ax.plot([r['pts'] - t for r in rows], [r['sync_signals'][cell] for r in rows], '.-', linewidth=.8)
            for key, level in appearance['thresholds'].items():
                ax.axvspan(level['before_pts'] - t, level['after_pts'] - t, alpha=.15, label=key)
            ax.set(title=f'New letter {i + 1}: original PTS50={t:.6f}; broad transition warning={appearance["transition_warning"]}',
                   xlabel='Seconds relative to this independent 50% appearance', ylabel='Cell dark fraction')
            ax.grid(alpha=.2); ax.legend(loc='upper left', ncol=3)
        fig.savefig(out / result['experiment'] / 'sync-signals.png', dpi=160); plt.close(fig)
    if any(r.get('policy') for r in results):
        fig,ax=plt.subplots(figsize=(10,5),constrained_layout=True)
        for result in results:
            valid=[r for r in result['frames'] if r['valid'] and r.get('input_relative_s') is not None]
            ax.plot([r['input_relative_s'] for r in valid],[r['regions']['group']['R'] for r in valid],'.-',
                linewidth=.8,markersize=2,label=result['experiment']+' '+str(result.get('policy')))
        ax.set(xlabel='Seconds since first following CHAR (inferred optical correspondence)',ylabel='Group R',
            title='Same input-start time basis; camera/optical offset not calibrated')
        ax.grid(alpha=.2);ax.legend();fig.savefig(out/'05-input-start.png',dpi=180);plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', required=True, type=Path)
    parser.add_argument('--output-dir', required=True, type=Path)
    parser.add_argument('--ffmpeg')
    args = parser.parse_args()
    config = json.loads(args.manifest.read_text(encoding='utf-8-sig'))
    if args.output_dir.exists():
        raise ValueError('new output directory required; prior evidence stays read-only')
    for entry in config['experiments']:
        for name in ('video', 'log', 'previous', 'corners'):
            if not Path(entry[name]).is_file():
                raise ValueError('missing input ' + entry[name])
    args.output_dir.mkdir(parents=True)
    results = [analyze_experiment(entry, args.output_dir / entry['id'], video.ffmpeg_path(args.ffmpeg))
               for entry in config['experiments']]
    plot_results(results, args.output_dir)
    (args.output_dir / 'manifest.json').write_text(json.dumps(config, indent=2), encoding='utf-8')
    summary = []
    for r in results:
        summary.append(dict(experiment=r['experiment'], frames=r['frame_count'], valid_frames=r['valid_frame_count'],
            sync_valid=r['synchronization']['valid'], sync=r['synchronization'],
            measurable_events=sum(e['measurable'] for e in r['events']),
            negative_events=sum(e['measurable'] and e['regions']['group']['delta_C'] < 0 for e in r['events']),
            events=r['events'], burst=r['burst'], no_update=r['no_update'], previous_R=r['previous_R'],
            guard_sensitivity=r['guard_sensitivity'], fade_onset=r['fade_onset'], post_update_segments=r['post_update_segments'],
            reconciliation=r['reconciliation']))
    (args.output_dir / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    print(json.dumps([{k: r[k] for k in ('experiment', 'frames', 'valid_frames', 'sync_valid', 'measurable_events', 'negative_events')}
                      for r in summary], indent=2))


if __name__ == '__main__':
    main()
