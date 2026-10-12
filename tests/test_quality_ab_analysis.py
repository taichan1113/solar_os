import unittest
from scripts.analyze_quality_ab import analyze
from test_fastest_quality_analysis import record


def batch(kind="A", waveform="baseline"):
    lines = [f"AB begin batch={kind} waveform={waveform} width=800 height=480 spi_khz=40000 path=tile max_fast=10",
             "AB ROI marker=400,160,16,16 near_black=320,160,48,48 far_black=32,160,48,48"]
    gen = 0
    def frame(phase, ordinal, cold, changes):
        nonlocal gen
        gen += 1
        lines.append(f"AB submit phase={phase} ordinal={ordinal} changed_pixels={changes} cold={cold}")
        if phase in ("marker", "initial-fast"):
            lines.append(record(start=gen*2000, waveform=waveform, cold=cold, region="400,160,16,16"))
        lines.append(f"AB complete phase={phase} ordinal={ordinal} err=0 generation={gen} completed={gen} total_us=1000 busy_us=700 convert_us=80 spi_us=200")
    frame("white",0,1,100)
    lines.append("AB image bytes=48000 native_fnv1a=12345678")
    if kind=="B": frame("initial-otp",0,1,100)
    else: frame("initial-fast",1,1,100)
    for step in range(1, 11 if kind=="B" else 10):
        lines.append(f"AB invariant step={step} fixed_black_00=1 fixed_white_11=1 fixed_bits=5000 outside_changes=0 changed_pixels=256")
        frame("marker",step if kind=="B" else step+1,int(kind=="B" and step==1),256)
    lines.append("AB finished fast_count=10: inspect before cleanup")
    lines.append("AB end err=0 fast_count=10 healthy=1")
    frame("exit-cleanup",0,0,0)
    lines.append(record(start=90000, waveform="hold")) # shell redraw must be excluded
    return "\n".join(lines)


class ABAnalysisTests(unittest.TestCase):
    def test_four_batches_scope_and_count(self):
        for kind in ("A","B"):
            for wf in ("baseline","dose"):
                r=analyze(batch(kind,wf))
                self.assertTrue(r["valid"], r["problems"])
                self.assertEqual((r["cold"],r["warm"]),(1,9))
                self.assertEqual(r["warm_stats"]["total_us"]["n"],9)
                self.assertEqual(r["metadata"]["waveform"],wf)

    def test_incomplete_or_aborted_not_pass(self):
        for text in (batch().split("AB finished")[0], batch().replace("fast_count=10","fast_count=9"),
                     batch().replace("phase=exit-cleanup", "phase=not-cleanup")):
            self.assertFalse(analyze(text)["valid"])

    def test_invariant_error_not_pass(self):
        for old,new in (("fixed_black_00=1","fixed_black_00=0"),
                        ("outside_changes=0","outside_changes=1"),
                        ("changed_pixels=256","changed_pixels=255")):
            self.assertFalse(analyze(batch().replace(old,new))["valid"])

    def test_uncompleted_fault_or_wrong_waveform_not_pass(self):
        for old,new in (("err=0 generation=3","err=263 generation=3"),
                        ("generation=3 completed=3","generation=3 completed=2"),
                        ("REF phases path=tile","REF phases path=scalar"),
                        ("waveform=baseline\nREF write","waveform=dose\nREF write")):
            text=batch().replace(old,new)
            self.assertNotEqual(text,batch())
            self.assertFalse(analyze(text)["valid"])

    def test_multiple_batches_rejected(self):
        with self.assertRaises(ValueError): analyze(batch()+batch())


if __name__=="__main__": unittest.main()
