import copy
import json
import os
from pathlib import Path
import sys
import unittest

HIL=Path(__file__).parent/'hil'
sys.path.insert(0,str(HIL))
import analyze_writer_quality as journal
import analyze_writer_contrast_timeline as timeline
import capture_writer_quality as capture
import measure_writer_video as video
from compare_writer_burst import compare
from model_writer_burst import simulate


class BurstModelTests(unittest.TestCase):
    def test_candidate_selection_counts_and_first_edge(self):
        normal,chosen=simulate(),simulate(160)
        self.assertEqual((normal['physical'],chosen['physical']),(5,4))
        self.assertEqual(normal['first_ms'],chosen['first_ms'])
        self.assertLessEqual(chosen['maximum_ms']-normal['maximum_ms'],60)
        self.assertEqual(simulate(120)['physical'],normal['physical'])
        self.assertGreater(simulate(200)['maximum_ms'],chosen['maximum_ms'])

    def test_single_input_and_100ms_spacing_never_wait_for_debounce(self):
        for times in ([0],[0,100,200,300],[0,60,400,460]):
            a,b=simulate(input_times=times),simulate(160,input_times=times)
            self.assertEqual(a['first_ms'],b['first_ms'])
            self.assertEqual(b['completed'][-1][1],len(times))
            self.assertLessEqual(b['maximum_ms'],272)

    def test_long_typing_is_bounded_and_trailing_snapshot_is_final(self):
        r=simulate(160,input_times=[60*i for i in range(40)])
        self.assertLessEqual(r['max_completion_gap_ms'],160)
        self.assertEqual(r['completed'][-1][1],40)
        self.assertLessEqual(r['maximum_ms'],272)

    def test_pending_membership_selects_one_sync_letter_per_update(self):
        physical=[dict(chars=n) for n in (20,21,24,27,29)]
        self.assertEqual(timeline.burst_sync_indices(physical),[0,3,6,8])
        for counts in ((20,21,21,29),(19,21,29),(20,21,28)):
            with self.assertRaises(ValueError):
                timeline.burst_sync_indices([dict(chars=n) for n in counts])

    def test_capture_only_authorized_current_batch_t0_can_auto_advance(self):
        state=dict(mode='burst',gate='first',starts=1,inputs=20,points={'T0':{}},fault=False)
        self.assertTrue(capture.ready_for_video_following(state))
        for key,value in (('gate','final'),('gate','white'),('fault',True),('starts',2),('auto_following_sent',True)):
            self.assertFalse(capture.ready_for_video_following({**state,key:value}))
        self.assertFalse(capture.allowed_control({**state,'gate':'final','fault':True},b'g'))


class BurstJournalTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root=Path(os.environ.get('WQ_BURST_HOST_DIR',str(HIL.parents[1]/'.research/writer-burst-coalescing/host')))
        if not (root/'baseline-burst.log').is_file():
            raise unittest.SkipTest('run real Writer Host fixture with WQ_OUTPUT_DIR first')
        cls.logs={p:(root/f'{p}-burst.log').read_text() for p in ('baseline','coalesced')}

    def test_real_fixture_counts_all_membership_and_latency(self):
        normal=journal.analyze(self.logs['baseline']);merged=journal.analyze(self.logs['coalesced'])
        for a in (normal,merged):
            self.assertTrue(a['valid'],a['errors'])
            self.assertEqual(a['protocol'],'writer-burst')
            self.assertEqual(len(a['response']['mapping']),29)
            self.assertEqual(a['response']['mapping'][-1]['seq'],29)
            self.assertEqual(a['input_count'],29)
            self.assertEqual(a['black_masks']['group']['black_pixels'],940)
            self.assertEqual(a['optical_R'],'未測定')
        self.assertEqual((normal['render_count'],merged['render_count']),(10,5))
        self.assertEqual((normal['physical_count'],merged['physical_count']),(6,5))
        self.assertEqual(normal['response']['first_us'],merged['response']['first_us'])
        self.assertLessEqual(merged['response']['maximum_us']-normal['response']['maximum_us'],60000)
        c=compare(normal,merged)
        self.assertEqual(c['physical_reduction'],1)
        self.assertIsNone(c['normal']['quality_R'])
        with self.assertRaises(ValueError):compare(merged,normal)
        wrong=copy.deepcopy(merged);wrong['black_masks']['group']['roi'][0]+=1
        with self.assertRaises(ValueError):compare(normal,wrong)

    def test_missing_duplicate_wrong_order_and_final_document_rejected(self):
        original=self.logs['coalesced']
        line=next(s for s in original.splitlines(True) if s.startswith('WQ input seq=25 '))
        mutation=next(s for s in original.splitlines(True) if s.startswith('WQ mutation seq=29 '))
        variants=[original.replace(line,''),original.replace(line,line+line),
                  original.replace('seq=25 ch=102','seq=25 ch=103'),
                  original.replace(mutation,mutation.replace('version=30','version=29')),
                  original.replace('WQ snapshot gen=','WQ snapshotbad gen=',1),
                  original.replace('chars=29','chars=28'),
                  original.replace('interval_ms=60','interval_ms=100'),
                  original.replace('healthy=1 restored=1','healthy=0 restored=0'),
                  original.replace('err=0 changes=unmeasured','err=1 changes=unmeasured',1),
                  original.replace('max_fast=10','max_fast=11')]
        for bad in variants:
            self.assertFalse(journal.analyze(bad)['valid'])

    def test_video_windows_match_elapsed_since_completions_for_both_policies(self):
        for text in self.logs.values():
            a=journal.analyze(text)
            points={p['name']:p for p in a['rows']['point']}
            sync=dict(markers=[dict(point=p,event='completed',video_time_s=5+points[p]['completed_us']/1e6)
                               for p in ('T0','T1')])
            windows,mapping=video.sync_windows(a,sync)
            self.assertAlmostEqual(mapping['video_per_device_second'],1)
            self.assertEqual([r['requested_after_completion_s'] for r in windows['T1']],[.5,1,1.5])
            self.assertIsNone(mapping['time_uncertainty_s'])
            selected=timeline.burst_sync_indices(a['rows']['physical'])
            self.assertEqual(len(selected),a['physical_count']-1)


if __name__=='__main__':
    unittest.main()
