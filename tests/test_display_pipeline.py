import importlib.util
from pathlib import Path
import unittest


SPEC = importlib.util.spec_from_file_location(
    "display_pipeline", Path(__file__).resolve().parents[1] / "scripts/analyze_display_pipeline.py")
PIPELINE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PIPELINE)


class PipelineCaptureTests(unittest.TestCase):
    def test_batched_keys_and_duplicate_output(self):
        capture = """
KEY_REPORT t=10 key=97 usage=4
KEY_REPORT t=12 key=98 usage=5
frame=2 WRITER_INPUT t=20 key=97
frame=2 WRITER_INPUT t=22 key=98
OTP fast err=0 start=110 end=190 total_us=80
OTP write0 start=120 end=140 us=20
OTP phases0 convert_start=120 convert_end=138 convert_us=6 spi_us=12
OTP activate0 start=140 ready=180 us=40
display=0x123 PRESENT_REQUEST t=101
display=0x123 PRESENT_START t=105
display=0x123 PRESENT_END t=195
frame=2 WRITER_RENDER_START t=30
frame=2 WRITER_RENDER_END t=100 clear_us=55
frame=2 PRESENT_RETURN t=200
frame=2 TICK start=25 end=201 doc_us=5 recovery_us=0 recovery_err=0
"""
        rows = PIPELINE.analyze(capture + capture)
        self.assertEqual(len(rows), 1)
        row = rows[0]
        self.assertEqual(row["inputs"], 2)
        self.assertEqual(row["input_to_render_us"], 10)
        self.assertEqual(row["render_us"], 70)
        self.assertEqual(row["document_us"], 5)
        self.assertEqual(row["scheduling_us"], 4)
        self.assertEqual(row["conversion_us"], 6)
        self.assertEqual(row["spi_us"], 12)
        self.assertEqual(row["activation_us"], 40)
        self.assertEqual(row["input_to_complete_us"], 180)
        self.assertEqual(row["last_input_to_complete_us"], 178)
        self.assertEqual(row["report_to_complete_us"], 190)
        self.assertEqual(len(PIPELINE.analyze_ticks(capture + capture)), 1)

    def test_idle_recovery_is_reported_separately(self):
        capture = "frame=2 TICK start=300 end=450 doc_us=0 recovery_us=149 recovery_err=0"
        ticks = PIPELINE.analyze_ticks(capture)
        self.assertEqual(len(ticks), 1)
        self.assertEqual(ticks[0]["recovery_us"], 149)
        self.assertEqual(PIPELINE.analyze(capture), [])

    def test_identical_skip_and_incomplete_capture(self):
        capture = """
frame=3 WRITER_INPUT t=10 key=130
frame=3 WRITER_RENDER_START t=20
frame=3 WRITER_RENDER_END t=25 clear_us=1
display=0x123 PRESENT_REQUEST t=26
display=0x123 PRESENT_START t=27
display=0x123 PRESENT_END t=28
frame=3 PRESENT_RETURN t=29
frame=4 WRITER_RENDER_START t=40
"""
        rows = PIPELINE.analyze(capture)
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["refresh_mode"], "skipped")
        self.assertEqual(rows[0]["refresh_us"], 0)
        self.assertIsNone(rows[0]["report_to_complete_us"])


if __name__ == "__main__":
    unittest.main()
