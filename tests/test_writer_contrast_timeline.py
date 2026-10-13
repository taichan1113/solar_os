import copy
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).parent / 'hil'))
import analyze_writer_contrast_timeline as timeline
import measure_writer_video as video


def mask(roi, pixels):
    x, y, w, h = roi
    rows = []
    for b in range(y, y + h):
        row = bytearray((w + 7) // 8)
        for a in range(x, x + w):
            if (a, b) in pixels:
                row[(a - x) // 8] |= 1 << (7 - (a - x) % 8)
        rows.append(row.hex())
    return dict(roi=list(roi), rows=rows, black_pixels=len(pixels))


def fixture_masks():
    first = {(x, y) for x in range(302, 308) for y in range(185, 196)}
    group = first | {(x, y) for x in range(312, 348) for y in range(185, 196) if (x + y) % 3}
    fixed = {(x, y) for x in range(166, 215) for y in range(184, 197) if (x * 3 + y) % 5 < 3}
    return dict(first=mask((300, 180, 10, 24), first), group=mask((300, 180, 60, 24), group),
                fixed=mask((160, 180, 60, 24), fixed))


def metric_rows(times, contrasts):
    return [dict(pts=t, valid=True, regions={name: dict(C=c, W=220, C_shift_min=c-.01, C_shift_max=c+.01)
                                           for name in ('first', 'group', 'fixed')}) for t, c in zip(times, contrasts)]


class TimelineTests(unittest.TestCase):
    def test_immutable_masks_and_separate_sync_roi(self):
        masks = fixture_masks(); original = copy.deepcopy(masks)
        black, cells = timeline.immutable_geometry(masks, 5)
        self.assertEqual(masks, original)
        self.assertEqual(cells[0], (361, 180, 8, 24))
        self.assertEqual(len(black['first']), masks['first']['black_pixels'])
        with self.assertRaises(ValueError):
            timeline.immutable_geometry({**masks, 'bad': masks['group']}, 5)
        with self.assertRaises(ValueError):
            timeline.immutable_geometry(masks, 50)

    def test_independent_appearance_unique_and_ambiguous(self):
        rows = [dict(pts=i/30, valid=True, sync_signals=[0 if i < 30 else .12]) for i in range(75)]
        result = timeline.detect_appearance(rows, 0, (.2, .6), (1.6, 2), (.7, 1.5))
        self.assertTrue(result['valid'], result)
        self.assertAlmostEqual(result['video_pts'], .98333333)
        self.assertAlmostEqual(result['bracket_s'], 1/30)
        # A second persistent appearance is not silently assigned to the log.
        for i in range(35, 40):
            rows[i]['sync_signals'][0] = 0
        self.assertFalse(timeline.detect_appearance(rows, 0, (.2, .6), (1.6, 2), (.7, 1.5))['valid'])
        for r in rows:
            r['sync_signals'][0] = 0
        self.assertFalse(timeline.detect_appearance(rows, 0, (.2, .6), (1.6, 2), (.7, 1.5))['valid'])

    def test_multievent_offset_and_failure_detected(self):
        events = [dict(end=int(t*1e6)) for t in (1, 1.5, 2, 2.5)]
        appearances = [dict(valid=True, video_pts=t+10+e, bracket_s=.034)
                       for t, e in zip((1, 1.5, 2, 2.5), (0, .01, -.01, 0))]
        result = timeline.fit_offset(appearances, events, 0)
        self.assertTrue(result['valid'])
        self.assertAlmostEqual(result['video_origin_s'], 10)
        self.assertAlmostEqual(result['residual_max_s'], .01)
        self.assertIsNone(result['absolute_latency_bound_s'])
        appearances[-1]['video_pts'] += .3
        self.assertFalse(timeline.fit_offset(appearances, events, 0)['valid'])
        self.assertFalse(timeline.fit_offset(appearances[:2], events[:2], 0)['valid'])
        appearances[-1]['video_pts'] = appearances[-2]['video_pts']
        self.assertFalse(timeline.fit_offset(appearances, events, 0)['valid'])

    def test_guard_rejects_driving_and_short_gaps_without_interpolation(self):
        events = [dict(index=i+1, gen=i+2, video_start=1+i*.2, video_end=1.112+i*.2,
                       visible_low=1+i*.2, visible_high=1.12+i*.2) for i in range(3)]
        gaps = timeline.stable_gaps(events, 0, 3, .17)
        rows = metric_rows([i/30 for i in range(91)], [.8]*91)
        windows = [timeline.summarize_window(rows, a, b) for a, b, _ in gaps]
        self.assertFalse(windows[1]['measurable'])
        self.assertFalse(windows[2]['measurable'])
        self.assertFalse(timeline.delta_event(windows[0], windows[1])['measurable'])
        self.assertEqual(timeline.phase_for(1.05, events, .01)[0], 'during_inferred_driver')
        self.assertEqual(timeline.phase_for(1.195, events, .02)[0], 'uncertain')
        self.assertTrue(all(not (e['video_start'] <= t <= e['video_end'])
                            for a,b,_ in gaps for t in [r['pts'] for r in rows if a<=r['pts']<=b] for e in events))

    def test_delta_and_no_update_rate_are_descriptive(self):
        before = timeline.summarize_window(metric_rows([0, .1, .2], [.8, .8, .8]), 0, .2)
        after = timeline.summarize_window(metric_rows([1, 1.1, 1.2], [.7, .7, .7]), 1, 1.2)
        result = timeline.delta_event(before, after)
        self.assertTrue(result['measurable'])
        self.assertAlmostEqual(result['regions']['group']['delta_C'], -.1)
        rows = metric_rows([0, 1, 2, 3], [.8, .7, .6, .5])
        rate = timeline.no_update_rate(rows, 0, 3)
        self.assertAlmostEqual(rate['descriptive_C_per_s'], -.1)
        self.assertFalse(timeline.no_update_rate(rows[:1], 0, 3)['measurable'])
        self.assertFalse(timeline.summarize_window(rows, 3.5, 4)['measurable'])

    def test_immediate_event_delta_excludes_late_post_update_recovery(self):
        rows=metric_rows([0,.1,.2,.3,1,1.1,1.2,10,20],[.8,.8,.8,.8,.7,.7,.7,.95,.95])
        events=[dict(index=1,gen=2,video_start=.5,video_end=.8)]
        result=timeline.event_measurements(rows,events,[(0,.3,0),(1,20,1)])[0]
        self.assertTrue(result['measurable'])
        self.assertAlmostEqual(result['regions']['group']['delta_C'],-.1)
        self.assertLessEqual(max(result['after']['actual_pts']),1.2)

    def test_sensitivity_never_promotes_missing_primary_frames(self):
        events = [dict(index=i+1, gen=i+2, video_start=1+i*.5, video_end=1.112+i*.5,
                       visible_low=1+i*.5, visible_high=1.12+i*.5) for i in range(3)]
        rows = metric_rows([i/30 for i in range(121)], [.8]*121)
        results = timeline.guard_sensitivity(rows, events, 0, 4, .05)
        self.assertEqual([r['assumed_systematic_s'] for r in results], [0,.05,.1])
        self.assertTrue(all(e['measurable'] for e in results[1]['events']))
        self.assertFalse(any(e['measurable'] for e in results[2]['events']))
        before = timeline.summarize_window(rows, .5, .7)
        for r in rows:
            if r['pts'] >= 1.1:r['regions']['group']['C'] = .6
        onset = timeline.fade_onset(rows, before, .8, 2)
        self.assertTrue(onset['measurable'])
        self.assertAlmostEqual(onset['pts_bracket'][1], 1.1)

    def test_white_changes_and_broad_visible_transition_are_not_hidden(self):
        rows = metric_rows([0,1,2,3], [.8,.8,.8,.8])
        for i,r in enumerate(rows):r['regions']['group']['W'] = 200+10*i
        rate = timeline.no_update_rate(rows, 0, 3)
        self.assertGreater(rate['white_edge_relative_change'], 0)
        self.assertEqual(rate['white_max'], 230)
        events = [dict(gen=2, video_start=1, video_end=1.1, visible_low=.6, visible_high=1.3)]
        self.assertEqual(timeline.phase_for(.7, events, .05), ('uncertain',2))
        signal = [dict(pts=i/30,valid=True,sync_signals=[0 if i<30 else (.02 if i<40 else .12)])
                  for i in range(80)]
        appearance = timeline.detect_appearance(signal,0,(.2,.6),(1.6,2),(.7,1.5))
        self.assertTrue(appearance['valid'])
        self.assertTrue(appearance['transition_warning'])

    @unittest.skipUnless(importlib.util.find_spec('numpy'), 'NumPy not installed')
    def test_vector_sampler_and_registration_match_existing_optical_kernel(self):
        import numpy as np
        from PIL import Image
        masks = fixture_masks()
        image = Image.new('L', (800,480), 220)
        for record in masks.values():
            for xy in video.optical.black_coordinates(record):
                image.putpixel(xy, 50)
        original = copy.deepcopy(masks)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'image.png'; image.save(path)
            corners = [[0,0],[800,0],[800,480],[0,480]]
            sample = video.frame_sampler(path,corners)
            old_pose = video.optical.register_mask(sample,masks['fixed'])
            engine = timeline.ImageMetrics(masks,corners)
            array = np.asarray(image,dtype=float)
            new_pose = engine.register(array)
            self.assertEqual((new_pose['dx'],new_pose['dy']), (old_pose['dx'],old_pose['dy']))
            metrics, pose, _ = engine.measure(array)
            shifted = lambda x,y:sample(x+pose['dx'],y+pose['dy'])
            for name, record in masks.items():
                old = video.optical.compare_roi(shifted,shifted,record)['T0']
                for key in ('C','W','B','white_MAD','black_MAD','C_shift_min','C_shift_max'):
                    self.assertAlmostEqual(metrics[name][key],old[key],places=10)
            with self.assertRaises(ValueError):
                engine.measure(np.full_like(array,220))
        self.assertEqual(masks,original)

    def test_decode_records_original_indices_and_variable_pts(self):
        try: exe=video.ffmpeg_path()
        except ValueError:self.skipTest('FFmpeg not supplied')
        with tempfile.TemporaryDirectory() as directory:
            movie=Path(directory)/'variable.mkv'
            video.ffmpeg_run(exe,['-f','lavfi','-i','color=c=white:s=96x64:r=10:d=2',
                '-vf','settb=1/1000,setpts=if(lt(N\\,10)\\,N*100\\,(1000+(N-10)*200))',
                '-fps_mode','vfr','-c:v','ffv1',movie])
            frames=list(timeline.decode_selected(movie,exe,(0,0,96,64),(.35,2.8),20,sparse_s=5))
            self.assertGreater(len(frames),2)
            self.assertEqual(frames[0][0:2],(0,0))
            self.assertTrue(any(index>=4 and .39<pts<.81 for index,pts,_ in frames))
            self.assertEqual(len({i for i,_,_ in frames}),len(frames))
            pts_by_index={index:pts for index,pts,_ in frames}
            self.assertAlmostEqual(pts_by_index[11],1.2)
            self.assertAlmostEqual(pts_by_index[12],1.4)
            self.assertNotEqual(pts_by_index[12],12/10) # nominal FPS cannot reconstruct this PTS
            for _,_,image in frames:self.assertEqual(image.size,(96,64))

    def test_prior_real_t0_t1_reconciliation(self):
        root=os.environ.get('WQ_TIMELINE_RESULTS')
        if not root:self.skipTest('real timeline results not supplied')
        for name in ('A1','A2','B2'):
            result=json.loads((Path(root)/name/'result.json').read_text())
            self.assertEqual(len(result['reconciliation']),6)
            for frame in result['reconciliation']:
                self.assertEqual(frame['matched'],1)
                self.assertLess(max(abs(v) for v in frame['delta_C'].values()),1e-8)
            self.assertEqual(result['masks']['group']['black_pixels'],940)
            self.assertTrue(result['synchronization']['valid'])
            for event in result['events']:
                if event['measurable']:
                    for window in ('before','after'):
                        for pts in event[window]['actual_pts']:
                            self.assertTrue(all(not e['video_start']<=pts<=e['video_end'] for e in result['events']))


if __name__=='__main__':unittest.main()
