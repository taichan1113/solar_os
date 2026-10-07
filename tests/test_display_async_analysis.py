import importlib.util
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location('async_analysis', Path(__file__).resolve().parents[1] / 'scripts/analyze_display_async.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class AsyncAnalysisTests(unittest.TestCase):
    def capture(self):
        return '''KEY_REPORT t=10 key=97 usage=4
frame=1 WRITER_INPUT t=12 key=97
frame=1 MUTATION_END t=13
frame=1 WRITER_RENDER_START t=20
frame=1 WRITER_RENDER_END t=30
gen=1 SNAPSHOT_SUBMIT t=31 copied_us=2 replaced=0
frame=1 SUBMIT_RETURN t=34 gen=1 err=0
gen=2 SNAPSHOT_SUBMIT t=40 copied_us=2 replaced=1
gen=3 SNAPSHOT_SUBMIT t=50 copied_us=2 replaced=2
gen=3 WORKER_START t=60
gen=3 WORKER_END t=100 err=0 physical=1
'''

    def test_replacement_chain_and_duplicates(self):
        text = self.capture()
        result = MODULE.analyze(text + text)
        self.assertEqual(result['counts']['submits'], 3)
        self.assertEqual(result['counts']['replaced'], 2)
        self.assertEqual(result['counts']['physical'], 1)
        self.assertEqual(result['frames'][0]['catchup_gen'], 3)
        self.assertEqual(result['inputs'][0]['dispatch_us'], 2)
        self.assertEqual(result['inputs'][0]['catchup_us'], 90)
        self.assertEqual(result['trials'][0]['frame_ids'], [1])
        self.assertEqual(result['trials'][0]['keys'], 'a')
        self.assertEqual(result['trials'][0]['first_report_to_complete_us'], 90)

    def test_cancelled_frame_not_assigned_unrelated_later_completion(self):
        result = MODULE.analyze(self.capture().replace('replaced=1', 'replaced=0'))
        self.assertIsNone(result['frames'][0]['completed_us'])

    def test_fault_incomplete_and_synthetic_are_not_success(self):
        for text in (self.capture().replace('err=0 physical=1', 'err=-1 physical=0'),
                     self.capture().replace('gen=3 WORKER_END t=100 err=0 physical=1', '')):
            self.assertIsNone(MODULE.analyze(text)['frames'][0]['completed_us'])
        result = MODULE.analyze(self.capture().replace('KEY_REPORT t=10 key=97 usage=4', ''))
        self.assertIsNone(result['dispatch_us'])

    def test_dispatch_during_physical_work_is_measured_separately(self):
        text = self.capture() + '\ngen=0 WORKER_START t=5\ngen=0 WORKER_END t=15 err=0 physical=1\n'
        result = MODULE.analyze(text)
        self.assertTrue(result['inputs'][0]['received_while_worker_busy'])
        self.assertEqual(result['busy_dispatch_us']['median'], 2)
        result = MODULE.analyze(text.replace('t=15 err=0 physical=1', 't=15 err=0 physical=0'))
        self.assertIsNone(result['busy_dispatch_us'])

    def test_interleaved_record_is_reported_without_inventing_completion(self):
        text = self.capture().replace('t=100 err=0 physical=1', 't51=100 err=0 physical=1')
        result = MODULE.analyze(text)
        self.assertEqual(len(result['malformed_events']), 1)
        self.assertEqual(result['malformed_events'][0]['missing'], ['t'])
        self.assertIsNone(result['frames'][0]['completed_us'])
        self.assertIsNone(result['trials'][0]['last_report_to_complete_us'])
        # A missing start cannot contribute a worker duration; a separately
        # intact end still establishes completion, not the unobserved duration.
        result = MODULE.analyze(self.capture().replace('WORKER_START t=60', 'WORKER_START t51=60'))
        self.assertEqual(result['counts']['workers'], 0)
        self.assertEqual(result['trials'][0]['last_report_to_complete_us'], 90)


if __name__ == '__main__':
    unittest.main()
