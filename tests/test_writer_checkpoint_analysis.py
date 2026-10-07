import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from analyze_writer_checkpoint import analyze


class CheckpointAnalysisTests(unittest.TestCase):
    def test_overlap_uses_reception_not_dispatch_and_deduplicates(self):
        lines = [
            'gen=1 ver=2 CHECKPOINT_WORKER_START t=100 err=0',
            'KEY_REPORT t=90 key=97',
            'frame=1 WRITER_INPUT t=150 key=97',
            'KEY_REPORT t=160 key=98',
            'frame=2 WRITER_INPUT t=310 key=98',
            'gen=1 ver=2 CHECKPOINT_COMPLETE t=300 err=0',
            'gen=2 ver=2 CHECKPOINT_WORKER_START t=400 err=0',
            'gen=1 ver=2 SNAPSHOT bytes=210 copy_us=30',
        ]
        result = analyze('\n'.join(lines + lines))
        self.assertEqual(result['checkpoints'], 1)
        self.assertEqual(result['unfinished'], 1)
        self.assertEqual(result['busy_dispatch_us']['n'], 1)
        self.assertEqual(result['busy_dispatch_us']['median'], 150)
        self.assertEqual(result['idle_dispatch_us']['median'], 60)
        self.assertEqual(result['copy_us']['n'], 1)
