import importlib.util
from pathlib import Path
import unittest


def load(name):
    spec=importlib.util.spec_from_file_location(name,Path(__file__).parent/"hil"/(name+".py"))
    module=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


analysis=load("analyze_writer_quality")
capture=load("capture_writer_quality")


def sample(mode="slow", merged=False):
    lines=[f"WQ begin waveform=baseline mode={mode} fast_interval_ms=60"]
    for i in range(1,11):
        t=i*200000
        lines.append(f"WQ input seq={i} ch={96+i} len={60+i} sent={t} accepted={t} mutated={t+1} request={t+1}")
        lines.append(f"WQ frame chars={i} gen={i} hash=x{i} render_start={t+2} render_end={t+3}")
    executed=[i for i in range(1,11) if not merged or i not in (3,6,9)]
    for ordinal,i in enumerate(executed,1):
        lines.append(f"WQ physical ordinal={ordinal} gen={i} chars={i} hash=x{i} start={i*200000+4} "
                     f"end={i*200000+120000} err=0 fixed_change=0 fixed00=100 fixed11=7400 "
                     f"first_change={50 if ordinal==1 else 0} first00={0 if ordinal==1 else 50} "
                     "writes=2 activations=1 spi_hz=40000000 path=tile total_us=112000 observer_us=1000 "
                     "busy_us=51000 convert_us=35000 spi_us=20000")
    lines.append(f"WQ summary inputs=10 renders=10 submitted=10 completed={len(executed)} physical={len(executed)} "
                 f"replaced={10-len(executed)} overflow=0")
    lines.append("WQ exit err=0 healthy=1 restored=1")
    return "\n".join(lines)


class WriterQualityTests(unittest.TestCase):
    def test_continuous_host_journal_points_masks_and_no_middle_gates(self):
        # An actual Host capture may be supplied by the caller, so check the
        # analyzer against real Writer raster rather than a second fake raster.
        import os
        root=os.environ.get('WQ_OPTICAL_HOST_DIR')
        if not root: self.skipTest('actual Writer optical fixture logs not supplied')
        for mode in ('slow','fast'):
            text=(Path(root)/f'baseline-{mode}-optical.log').read_text()
            result=analysis.analyze(text)
            self.assertTrue(result['valid'],result['errors'])
            self.assertEqual(result['protocol'],'continuous-optical')
            self.assertEqual([p['name'] for p in result['rows']['point']],['T0','T1'])
            self.assertEqual(result['black_masks']['first']['black_pixels'],47)
            for bad in (text.replace('name=T1','name=T0'),
                        text.replace('ready_us=','ready_us=0 ignored=',1),
                        text.replace('WQ mask which=first row=0','WQ mask which=first row=99'),
                        text.replace('stage=final','stage=plus3')):
                self.assertFalse(analysis.analyze(bad)['valid'])

    def test_truncated_uart_records_fail_without_inventing_timing(self):
        bad=sample().replace("total_us=112000 ", "").replace("\nWQ physical", "WQ physical")
        result=analysis.analyze(bad)
        self.assertFalse(result["valid"])
        self.assertIsNone(result["driver_p50_us"])
        self.assertEqual(result["physical_count"],10)
        self.assertIn("truncated physical timing/transfer record",result["errors"])

    def test_actual_completions_and_replaced_requests_are_separate(self):
        slow=analysis.analyze(sample())
        fast=analysis.analyze(sample("fast",True))
        self.assertTrue(slow["valid"],slow["errors"])
        self.assertTrue(fast["valid"],fast["errors"])
        self.assertEqual((fast["input_count"],fast["physical_count"],fast["replaced"]),(10,7,3))
        self.assertEqual(fast["first_following_updates"],6)
        self.assertEqual(fast["optical_R"],"未測定")

    def test_no_pending_or_failed_frame_counted_as_complete(self):
        bad=sample().replace("chars=10 hash=x10", "chars=9 hash=x9")
        self.assertFalse(analysis.analyze(bad)["valid"])
        self.assertFalse(analysis.analyze(sample().replace("err=0 fixed_change", "err=263 fixed_change"))["valid"])

    def test_roi_pixel_changes_are_not_optical_fading(self):
        for before,after in (("fixed_change=0","fixed_change=1"),("first00=50","first00=49")):
            self.assertFalse(analysis.analyze(sample().replace(before,after,1))["valid"])

    def test_slow_input_must_follow_completion(self):
        self.assertFalse(analysis.analyze(sample().replace("sent=400000 accepted=400000", "sent=200001 accepted=200001"))["valid"])

    def test_mapping_replacement_cap_and_restore(self):
        for before,after in (("gen=10 chars=10","gen=99 chars=10"),("replaced=0","replaced=1"),
                             ("overflow=0","overflow=1"),("restored=1","restored=0")):
            self.assertFalse(analysis.analyze(sample().replace(before,after))["valid"])

    def test_capture_requires_one_explicit_gate_step(self):
        state={"gate":"first","fault":False}
        self.assertTrue(capture.allowed_control(state,b"g"))
        self.assertFalse(capture.allowed_control(state,b"gg"))
        state["gate"]=None
        self.assertFalse(capture.allowed_control(state,b"g"))
        state.update(gate="final",fault=True)
        self.assertFalse(capture.allowed_control(state,b"g"))
        self.assertTrue(capture.allowed_control(state,b"q"))

    def test_capture_fault_and_end(self):
        state={"fault":False}
        capture.parse_line(state,"WQ gate=plus3 inputs=4 renders=4 starts=3 g=next q=abort")
        self.assertEqual(state["gate"],"plus3")
        capture.parse_line(state,"WQ exit err=263 healthy=0 restored=0")
        self.assertTrue(state["fault"] and state["ended"])
        self.assertIsNone(state["gate"])


if __name__=="__main__":
    unittest.main()
