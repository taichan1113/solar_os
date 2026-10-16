"""Synthetic log validation only; these are not device latency measurements."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).parent / 'hil'))
import analyze_sketch_trace as trace


def journal(*, overflow=0, push_error=False, final=False,
            unchanged_release=False):
    entries = [
        (10, 'SAMPLE', 1, 1, 0, 100, 100),
        (11, 'PUSH', 1, 0, 0, trace.PRESS, 1),
        (12, 'POP', 1, 0, 0, trace.PRESS, 0),
        (13, 'DISPATCH', 1, 0, 0, trace.PRESS, 0),
        (14, 'CANVAS', 1, 1, 0, 2, 0),
        (15, 'EVENT', 1, 1, 0, 3, trace.PRESS),
        (20, 'SAMPLE', 2, 1, 0, 120, 100),
        (21, 'PUSH', 2, 0, 0, trace.RELEASE, -257 if push_error else 1),
    ]
    if not push_error:
        entries += [(22, 'POP', 2, 0, 0, trace.RELEASE, 0),
                    (23, 'DISPATCH', 2, 0, 0, trace.RELEASE, 0),
                    (24, 'EVENT', 2, 1, 0, 2, trace.RELEASE)]
    if final:
        entries += [(30, 'RENDER', 0, 1, 0, 0, 0),
                    (35, 'SUBMIT', 0, 1, 7, 5, 0),
                    (40, 'WORKER_START', 0, 0, 7, 0, 0),
                    (45, 'SSD', 0, 0, 0, 5, 0),
                    (46, 'WORKER_END', 0, 0, 7, 0, 1)]
    if unchanged_release:
        entries = [(t + 30 if point == 2 else t, name, point,
                    version, generation, a, b)
                   for t, name, point, version, generation, a, b in entries]
        entries.sort(key=lambda entry: entry[0])
    lines = [f'SKTRACE_META,0,{len(entries)},{overflow},30,4']
    for i, (t, name, point, version, generation, a, b) in enumerate(entries):
        lines.append(f'SKTRACE,{i},{t},{trace.NAMES.index(name)},{point},'
                     f'{version},{generation},{a},{b}')
    return lines


class SketchTraceTests(unittest.TestCase):
    def test_complete_stroke_and_generation(self):
        meta, rows = trace.parse(journal(final=True))
        result = trace.analyze(meta, rows)
        self.assertTrue(result['valid'], result['failures'])
        self.assertEqual(result['final_canvas_version'], 1)
        self.assertEqual(result['completed_canvas_version'], 1)
        self.assertEqual(result['stage_us']['release_to_done']['p50_us'], 26)

    def test_unchanged_release_uses_completed_prior_canvas(self):
        meta, rows = trace.parse(journal(final=True, unchanged_release=True))
        result = trace.analyze(meta, rows)
        self.assertTrue(result['valid'], result['failures'])
        self.assertEqual(result['stage_us']['release_to_done']['p50_us'], 0)

    def test_release_fifo_failure_is_not_success(self):
        meta, rows = trace.parse(journal(push_error=True, final=True))
        result = trace.analyze(meta, rows)
        self.assertFalse(result['valid'])
        self.assertEqual(result['fifo_failures'], 1)

    def test_missing_final_frame_is_not_success(self):
        meta, rows = trace.parse(journal())
        self.assertFalse(trace.analyze(meta, rows)['valid'])

    def test_nonphysical_completion_is_not_final_frame(self):
        lines = journal(final=True)
        lines[-1] = lines[-1][:-1] + '0'
        meta, rows = trace.parse(lines)
        result = trace.analyze(meta, rows)
        self.assertFalse(result['valid'])
        self.assertEqual(result['completed_canvas_version'], 0)

    def test_stroke_id_mismatch_is_rejected(self):
        lines = journal(final=True)
        lines[1] = lines[1].replace(',1,1,0,100,100', ',1,2,0,100,100')
        meta, rows = trace.parse(lines)
        self.assertIn('point 1 stroke ID mismatch', trace.analyze(meta, rows)['failures'])

    def test_i2c_error_is_reported_separately(self):
        meta, rows = trace.parse(journal(final=True))
        rows.insert(0, dict(index=-1, time_us=9, kind='READ', point=0,
                            version=0, generation=0, a=300, b=257))
        result = trace.analyze(meta, rows)
        self.assertEqual(result['read_failures'], 1)
        self.assertIn('FT6336 I2C read failed 1 time(s)', result['failures'])

    def test_overflow_or_missing_dump_is_not_synthesized(self):
        meta, rows = trace.parse(journal(overflow=1, final=True))
        self.assertFalse(trace.analyze(meta, rows)['valid'])
        bad = journal(final=True)
        del bad[-1]
        with self.assertRaisesRegex(ValueError, 'missing dump rows'):
            trace.parse(bad)

    def test_nonmonotonic_or_duplicate_row_is_rejected(self):
        bad = journal(final=True)
        bad[-1] = bad[-1].replace(',46,', ',1,')
        with self.assertRaisesRegex(ValueError, 'non-monotonic'):
            trace.parse(bad)
        bad = journal(final=True)
        bad.append(bad[-1])
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            trace.parse(bad)


if __name__ == '__main__':
    unittest.main()
