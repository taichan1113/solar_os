import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("touchraw", Path(__file__).parent / "hil/analyze_touchraw.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

HEADER = "TOUCHRAW status reads=2 stored=2 dropped=0 failures=1 max_consecutive=1\n"
FIRST = "TOUCHRAW sample seq=1 start=100 end=120 duration=20 interval=0 error=0 measured=1 raw=01802a303305064046f0500b0c result=1 reasons=0 p1=3,42,51,2,1,1 p2=15,70,80,1,0,0\n"
SECOND = "TOUCHRAW sample seq=2 start=1100 end=1120 duration=20 interval=1000 error=-1 measured=0 raw=00000000000000000000000000 result=0 reasons=0 p1=0,0,0,0,0,0 p2=0,0,0,0,0,0\n"
END = "TOUCHRAW end emitted=2 stored=2 dropped=0\n"


class TouchRawAnalysisTest(unittest.TestCase):
    def test_raw_decode_error_and_unreported_slot(self):
        r = module.analyze(HEADER + FIRST + SECOND + END)
        self.assertEqual(r["counts"], {1: 1})
        self.assertEqual(r["reported_sentinel"], 0)
        self.assertEqual(r["sentinel_all_slots"], 1)
        self.assertEqual(r["all_slot_flags"], {2: 1, 1: 1})
        self.assertEqual(r["read_duration"]["p50_us"], 20)
        self.assertEqual(r["read_start_interval"]["p95_us"], 1000)

    def test_missing_end_or_record_rejected(self):
        for text in (HEADER + FIRST + SECOND, HEADER + FIRST + END):
            with self.assertRaises(ValueError):
                module.analyze(text)

    def test_sequence_timestamp_decoder_and_contact_mismatch(self):
        text = HEADER + FIRST + SECOND + END
        for old, new in (("seq=2", "seq=3"), ("interval=1000", "interval=999"),
                         ("duration=20", "duration=21"), ("result=1", "result=2"),
                         ("p1=3,42", "p1=3,43"), ("measured=0", "measured=1")):
            with self.subTest(old=old), self.assertRaises(ValueError):
                module.analyze(text.replace(old, new))

    def test_counted_up_remains_uncertain(self):
        first = FIRST.replace("raw=01802a", "raw=01402a").replace("result=1 reasons=0", "result=2 reasons=64").replace("p1=3,42,51,2", "p1=3,42,51,1")
        r = module.analyze(HEADER + first + SECOND + END)
        self.assertEqual(r["reported_up_counts"], {1: 1})
        self.assertEqual(r["decoder_results"], {2: 1})

    def test_duplicate_sentinel_and_reserved_invalid(self):
        # Actual static decoder contract is independently checked in dump validation.
        first = FIRST.replace("raw=01802a303305064046f0500b0c", "raw=02c02af03305064046f0500b0c").replace("result=1 reasons=0", "result=0 reasons=120").replace("p1=3,42,51,2,1,1", "p1=15,42,51,3,1,0").replace("p2=15,70,80,1,0,0", "p2=15,70,80,1,1,0")
        r = module.analyze(HEADER + first + SECOND + END)
        self.assertEqual(r["duplicate_id_frames"], 1)
        self.assertEqual(r["reported_sentinel"], 2)

    def test_read_drop_and_error_summary_mismatch(self):
        for header in (HEADER.replace("reads=2", "reads=3"), HEADER.replace("failures=1", "failures=0")):
            with self.assertRaises(ValueError):
                module.analyze(header + FIRST + SECOND + END)


if __name__ == "__main__":
    unittest.main()
