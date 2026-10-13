"""Host-only Writer scheduler checks using the completed trial journals."""
import copy
import json
import os
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).parent/'hil'))
import model_writer_feasibility as model


class FeasibilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root=Path(os.environ.get('WRITER_FEASIBILITY_ROOT',
             Path(__file__).resolve().parents[1]/'.research/writer-burst-coalescing'))
        if not (root/'C0/analysis.json').is_file() or not (root/'C1/analysis.json').is_file():
            raise unittest.SkipTest('completed C0/C1 hardware journals unavailable')
        cls.c0=json.loads((root/'C0/analysis.json').read_text(encoding='utf-8'))
        cls.c1=json.loads((root/'C1/analysis.json').read_text(encoding='utf-8'))
        cls.result=model.evaluate([cls.c0,cls.c1])

    def test_replay_reconciles_generations_replacements_and_responses(self):
        for name,source in (('C0',self.c0),('C1',self.c1)):
            replay=self.result['replay'][name]
            self.assertTrue(replay['pending_replacement_verified'])
            self.assertTrue(replay['final_snapshot'])
            self.assertEqual(replay['replay_error_us'],0)
            self.assertEqual(replay['replaced'],source['replaced'])
            self.assertEqual(replay['physical'],source['physical_count']-1)
            for key in ('first_us','p50_us','p95_us','maximum_us','final_us','max_no_update_us'):
                self.assertEqual(replay[key],source['response'][key])

    def test_replay_rejects_corrupt_version_or_missing_input(self):
        bad=copy.deepcopy(self.c0)
        bad['rows']['physical'][-1]['hash']='00000000'
        with self.assertRaisesRegex(ValueError,'hash'):model.replay(bad)
        bad=copy.deepcopy(self.c0)
        del bad['rows']['input'][-1]
        with self.assertRaises(ValueError):model.replay(bad)

    def test_predictive_model_is_close_on_both_recorded_paths(self):
        for source,policy in (('normal','normal'),('coalesced','trial160')):
            actual=self.result['replay']['C0' if source=='normal' else 'C1']
            pred=next(r for r in self.result['simulations'] if r['source']==source and r['policy']==policy)
            self.assertEqual(pred['physical'],actual['physical'])
            self.assertEqual(pred['replaced'],actual['replaced'])
            self.assertEqual(pred['physical_chars'],actual['physical_chars'])
            for key in ('first_us','p50_us','p95_us','maximum_us','final_us','max_no_update_us'):
                self.assertLessEqual(abs(pred[key]-actual[key]),10000,key)

    def test_selected_candidate_fails_robustness_not_just_one_phase(self):
        summaries=self.result['scenarios']['summary']
        a=next(row for row in summaries if row['policy']=='A120' and row['scenario']=='burst60')
        jitter=next(row for row in summaries if row['policy']=='A120' and row['scenario']=='burst60_jitter')
        self.assertEqual(a['physical_range'],[4,4])
        self.assertLess(a['go_cases'],a['cases'])
        self.assertEqual(jitter['go_cases'],0)
        for policy in ('B160fastTrailing','CpendingGate'):
            row=next(r for r in summaries if r['policy']==policy and r['scenario']=='burst60')
            self.assertEqual(row['go_cases'],0)

    def test_single_pause_100ms_and_edit_snapshots(self):
        p=self.result['parameters']
        work={key:p[key] for key in ('warm_us','submit_us','startup_us','input_pipeline_us')}
        for times,ops in (([0],'x'),([0,100000,200000,300000],'abcd'),
                          ([0,60000,120000,360000,420000],'abcde'),
                          ([0,60000,120000,180000,240000],'ab\b\nc')):
            ticks=list(range(40800,times[-1]+800000,40000))
            for policy in (dict(),dict(period_us=120000,idle_us=80000),
                           dict(period_us=160000,idle_us=40000),
                           dict(pending_gate=True)):
                result=model.simulate(times,ticks,operations=ops,**work,**policy)
                self.assertTrue(result['final_snapshot'])
                self.assertEqual(result['inputs'],len(times))
                self.assertEqual(result['latency_us'][-1],result['final_us'])

if __name__=='__main__':unittest.main()
