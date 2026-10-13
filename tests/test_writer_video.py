import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

hil=Path(__file__).parent/'hil'
sys.path.insert(0,str(hil))
import analyze_writer_quality as journal
import measure_writer_video as video
import writer_video_audio as audio
import capture_writer_quality as capture


class VideoTests(unittest.TestCase):
    def test_following_auto_gate_never_starts_another_batch_or_cleanup(self):
        state=dict(mode='video',gate='first',starts=1,inputs=20,points={'T0':{}},fault=False)
        self.assertTrue(capture.ready_for_video_following(state))
        for key,value in (('gate','final'),('gate','reference'),('mode','slow'),
                          ('starts',2),('inputs',19),('points',{}),('fault',True),
                          ('auto_following_sent',True)):
            self.assertFalse(capture.ready_for_video_following({**state,key:value}))

    def test_audio_candidates_require_unique_ordered_recorded_cues(self):
        import math
        rate=8000
        samples=[0]*rate
        for frequency in (880,1100):
            samples.extend(round(2000*math.sin(2*math.pi*frequency*i/rate)) for i in range(1600))
            samples.extend([0]*rate)
        cues=audio.find_cues(samples)
        self.assertEqual([r['point'] for r in cues],['T0','T1'])
        self.assertAlmostEqual(cues[0]['video_time_s'],1,delta=.06)
        self.assertAlmostEqual(cues[1]['video_time_s'],2.2,delta=.06)
        self.assertEqual(audio.find_cues([0]*rate),[])

    def analysis(self):
        return dict(rows={'point':[dict(name='T0',ready_us=3000000,completed_us=1000000),
                                   dict(name='T1',ready_us=6000000,completed_us=4000000)]})

    def test_sync_maps_completed_intervals_without_clock_or_camera_lock(self):
        sync=dict(method='audio',markers=[dict(point='T0',video_time_s=4,uncertainty_s=.1),
                                         dict(point='T1',video_time_s=7,uncertainty_s=.1)],
                  uart_and_cue_delay_bound_s=.03)
        windows,mapping=video.sync_windows(self.analysis(),sync)
        self.assertEqual([x['requested_video_time_s'] for x in windows['T0']],[2.5,3,3.5])
        self.assertEqual([x['requested_video_time_s'] for x in windows['T1']],[5.5,6,6.5])
        self.assertAlmostEqual(mapping['time_uncertainty_s'],.13)
        # Single manually identified transition can be exploratory, not exact.
        windows,mapping=video.sync_windows(self.analysis(),dict(markers=sync['markers'][:1]))
        self.assertIsNone(mapping['time_uncertainty_s'])
        windows,mapping=video.sync_windows(self.analysis(),dict(method='approximate visual completion',
            markers=[dict(point='T0',event='completed',video_time_s=2)]))
        self.assertEqual([x['requested_video_time_s'] for x in windows['T0']],[2.5,3,3.5])
        self.assertIsNone(mapping['time_uncertainty_s'])
        with self.assertRaises(ValueError):video.sync_windows(self.analysis(),dict(markers=[
            dict(point='T0',event='unknown',video_time_s=2)]))

    def test_sync_rejects_missing_nonmonotonic_and_duplicate_markers(self):
        for sync in ({},dict(markers=[dict(point='T0',video_time_s=-1)]),
                     dict(markers=[dict(point='T0',video_time_s=4),dict(point='T1',video_time_s=2)]),
                     dict(markers=[dict(point='T0',video_time_s=4)]*2)):
            with self.assertRaises(ValueError):video.sync_windows(self.analysis(),sync)
        with self.assertRaises(ValueError):video.sync_windows(self.analysis(),dict(markers=[dict(point='T0',video_time_s=4)]),(0,))

    def test_white_variation_flagged_without_independent_sample_claim(self):
        frames=[]
        for point,C,W in (('T0',.8,200),('T0',.8,200),('T1',.78,150),('T1',.75,240)):
            frames.append(dict(point=point,regions={'group':dict(C=C,W=W,white_MAD=1)}))
        result=video.summarize_frames(frames)
        self.assertEqual(result['independent_trial_count'],1)
        self.assertTrue(result['correlated_frames'])
        self.assertAlmostEqual(result['regions']['group']['R'],.765/.8)
        self.assertTrue(result['regions']['group']['uncertainty_flags'])
        self.assertEqual(result['quantitative_verdict'],'withheld')

    def test_actual_host_video_count_masks_spacing_and_corruption(self):
        root=os.environ.get('WQ_VIDEO_HOST_DIR')
        if not root:self.skipTest('actual Writer video Host logs not supplied')
        for count,interval in ((5,200),(9,200),(5,500)):
            text=(Path(root)/f'baseline-video-{count}-{interval}.log').read_text()
            result=journal.analyze(text)
            self.assertTrue(result['valid'],result['errors'])
            self.assertEqual(result['physical_count'],count+1)
            self.assertEqual(result['black_masks']['group']['black_pixels'],940)
            self.assertEqual(result['black_masks']['first']['roi'],[304,202,10,22])
            self.assertEqual(result['black_masks']['fixed']['roi'],[164,202,120,22])
            self.assertTrue(all(t==interval*1000 for t in result['burst_update_intervals_us']))
            for old,new in (('group_change=0','group_change=1'),('group00=940','group00=939'),
                            ('aborted=0','aborted=1'),('replaced=0','replaced=1'),
                            ('formation=20','formation=19'),('interval_ms=200','interval_ms=201')):
                if old not in text:continue
                self.assertFalse(journal.analyze(text.replace(old,new,1))['valid'])
            lines=text.splitlines()
            # A dropped physical row must not count as complete.
            broken='\n'.join(l for l in lines if not l.startswith('WQ physical ordinal=2 '))
            self.assertFalse(journal.analyze(broken)['valid'])

    def test_ffmpeg_actual_pts_and_decoded_dimensions(self):
        try:exe=video.ffmpeg_path()
        except ValueError:self.skipTest('FFmpeg not supplied')
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);movie=root/'sample.mkv'
            video.ffmpeg_run(exe,['-f','lavfi','-i','color=c=white:s=96x64:r=10:d=2',
                                 '-c:v','ffv1',movie])
            meta=video.probe_video(movie,exe)
            self.assertEqual(meta['decoded_size'],[96,64])
            self.assertAlmostEqual(meta['duration_s'],2)
            pts=video.extract_frame(movie,.56,root/'frame.png',exe)
            self.assertAlmostEqual(pts,.6)
            with self.assertRaises(ValueError):video.extract_frame(movie,3,root/'missing.png',exe)
            with self.assertRaises(ValueError):video.extract_frame(movie,.5,root/'frame.png',exe)

    def test_full_video_pipeline_uses_immutable_host_masks(self):
        try:exe=video.ffmpeg_path()
        except ValueError:self.skipTest('FFmpeg not supplied')
        source=os.environ.get('WQ_VIDEO_HOST_DIR')
        if not source:self.skipTest('actual Writer video Host logs not supplied')
        from PIL import Image
        analysis=journal.analyze((Path(source)/'baseline-video-5-200.log').read_text())
        self.assertTrue(analysis['valid'])
        analysis['rows']['point']=[dict(name='T0',completed_us=1000000,ready_us=3000000),
                                 dict(name='T1',completed_us=3000000,ready_us=5000000)]
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);image=Image.new('L',(800,480),220)
            for mask in analysis['black_masks'].values():
                for x,y in video.optical.black_coordinates(mask):image.putpixel((x,y),40)
            image.save(root/'source.png')
            video.ffmpeg_run(exe,['-loop','1','-i',root/'source.png','-t','6','-r','10',
                                 '-c:v','ffv1',root/'sample.mkv'])
            (root/'analysis.json').write_text(json.dumps(analysis))
            (root/'sync.json').write_text(json.dumps(dict(markers=[dict(point='T0',video_time_s=3),
                                                                  dict(point='T1',video_time_s=5)])))
            corners=[[0,0],[800,0],[800,480],[0,480]]
            (root/'corners.json').write_text(json.dumps(dict(T0=corners,T1=corners)))
            run=subprocess.run([sys.executable,'-B',str(hil/'measure_writer_video.py'),
                '--ffmpeg',exe,'--video',str(root/'sample.mkv'),'--analysis',str(root/'analysis.json'),
                '--sync',str(root/'sync.json'),'--corners',str(root/'corners.json'),
                '--output-dir',str(root/'result')],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr+run.stdout)
            result=json.loads((root/'result/result.json').read_text())
            self.assertEqual(len(result['frames']),6)
            self.assertEqual(result['frames'][0]['regions']['group']['black_pixels'],940)
            self.assertAlmostEqual(result['regions']['group']['R'],1)
            self.assertTrue(result['uncertainties']) # Uncalibrated sync stays explicit.

    def test_missing_original_does_not_create_density_output(self):
        try:exe=video.ffmpeg_path()
        except ValueError:self.skipTest('FFmpeg not supplied')
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);analysis=root/'analysis.json'
            analysis.write_text(json.dumps(dict(valid=True,protocol='writer-video')))
            run=subprocess.run([sys.executable,'-B',str(hil/'measure_writer_video.py'),
                '--ffmpeg',exe,'--video',str(root/'missing.mov'),'--analysis',str(analysis),
                '--sync',str(root/'sync.json'),'--corners',str(root/'corners.json'),
                '--output-dir',str(root/'result')],capture_output=True,text=True)
            self.assertNotEqual(run.returncode,0)
            self.assertFalse((root/'result').exists())


if __name__=='__main__':unittest.main()
