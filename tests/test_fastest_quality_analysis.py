import unittest
from scripts.analyze_fastest_quality import analyze, stats


def record(start=100, waveform="baseline", cold=0, err=0, region="0,0,8,8"):
    return (f"REF err={err} cold={cold} region={region} bytes=96000 start={start} end={start+1000} total_us=1000 busy_us=700\n"
            f"REF phases path=tile spi_khz=40000 diff_us=10 promote_us=2 waveform={waveform}\n"
            "REF write0 us=140 convert_us=40 spi_us=100\n"
            "REF write1 us=140 convert_us=40 spi_us=100\n")


class QualityAnalysisTests(unittest.TestCase):
    def test_percentile_and_empty(self):
        self.assertIsNone(stats([]))
        s = stats(list(range(1, 21)))
        self.assertEqual((s["n"], s["p50"], s["p95"], s["max"]), (20, 10.5, 19, 20))

    def test_duplicates_and_stage_sums(self):
        r = analyze(record() * 2)
        self.assertEqual(r["records"], 1)
        self.assertEqual(r["warm_complete"], 1)
        g = r["driver_groups"][0]
        self.assertEqual(g["convert_us"]["p50"], 80)
        self.assertEqual(g["spi_us"]["p50"], 200)
        self.assertIsNone(r["pipeline"]["input_to_complete_us"])

    def test_errors_cold_and_incomplete_not_success(self):
        text = record(cold=1) + record(start=2000, err=-1)
        text += record(start=4000).split("REF write1")[0]
        r = analyze(text)
        self.assertEqual(r["warm_complete"], 0)
        self.assertEqual(len(r["errors"]), 1)
        self.assertEqual(len(r["incomplete"]), 1)
        self.assertEqual(r["cold"], 1)

    def test_waveform_and_region_are_separate(self):
        r = analyze(record() + record(start=2000, waveform="dose") +
                    record(start=4000, region="0,0,800,480"))
        self.assertEqual(len(r["driver_groups"]), 3)

    def test_missing_metadata_and_bad_arithmetic(self):
        r = analyze(record().replace(" waveform=baseline", ""))
        self.assertEqual(r["warm_complete"], 0)
        r = analyze(record().replace("end=1100", "end=1099"))
        self.assertEqual(r["warm_complete"], 0)
        self.assertTrue(r["incomplete"])

    def test_truncated_line_does_not_borrow_next_metadata(self):
        r = analyze(record() + "REF err=0 cold=0 region=0,0,8,8 bytes=96000 start=5\nREF phases path=tile spi_khz=40000 waveform=hold\n")
        self.assertEqual(r["driver_groups"][0]["waveform"], "baseline")
        self.assertEqual(len(r["malformed"]), 1)

    def test_async_replacements_copy_and_failed_completion(self):
        text = '''KEY_REPORT t=10 key=97
frame=1 WRITER_INPUT t=12 key=97
frame=1 MUTATION_END t=13
frame=1 WRITER_RENDER_START t=20
frame=1 WRITER_RENDER_END t=30
gen=1 SNAPSHOT_SUBMIT t=31 copied_us=2 replaced=0
frame=1 SUBMIT_RETURN t=34 gen=1 err=0
gen=2 SNAPSHOT_SUBMIT t=40 copied_us=4 replaced=1
gen=2 WORKER_START t=60
gen=2 WORKER_END t=100 err=0 physical=1
'''
        r = analyze(text + text)
        p = r["pipeline"]
        self.assertEqual(p["input_dispatch_us"]["p50"], 2)
        self.assertEqual(p["input_to_complete_us"]["p50"], 90)
        self.assertEqual(p["writer_render_us"]["p50"], 10)
        self.assertEqual(p["submit_us"]["p50"], 4)
        self.assertEqual(p["snapshot_copy_us"]["p95"], 4)
        self.assertEqual(p["snapshot_copy_us"]["n"], 2)
        failed = analyze(text.replace("err=0 physical=1", "err=-1 physical=0"))
        self.assertIsNone(failed["pipeline"]["input_to_complete_us"])


if __name__ == "__main__":
    unittest.main()
