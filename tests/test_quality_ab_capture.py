import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch


class CaptureTests(unittest.TestCase):
    def test_exit_only_aborts_at_white_and_cannot_send_g(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            control=root/"control"
            status=root/"status.json"
            class FakeSerial:
                in_waiting=1
                def __init__(self):
                    self.writes=[]
                    self.samples=iter((b"AB gate=white g=next q=abort\n",
                                      b"AB end err=0 fast_count=0 healthy=1\nuser@sol:/ "))
                def open(self): pass
                def write(self,data): self.writes.append(data)
                def read(self,n):
                    data=next(self.samples)
                    if b"gate=white" in data: control.write_bytes(b"g")
                    return data
                def __enter__(self): return self
                def __exit__(self,*args): pass
            serial=FakeSerial()
            spec=importlib.util.spec_from_file_location("capture_exit",Path(__file__).parent/"hil"/"capture_quality_ab.py")
            module=importlib.util.module_from_spec(spec)
            with patch.dict(sys.modules,serial=types.SimpleNamespace(Serial=lambda:serial)):
                spec.loader.exec_module(module)
            argv=["capture","--port","COM39","--batch","A","--waveform","baseline","--exit-only",
                  "--log",str(root/"capture.log"),"--control",str(control),"--status",str(status)]
            with patch.object(sys,"argv",argv), patch.object(module.time,"sleep",lambda _:None):
                module.main()
            self.assertEqual(serial.writes,[b"display test display0 ab-a baseline\r",b"q"])
            state=json.loads(status.read_text())
            self.assertTrue(state["done"] and state["exit_only"])
            self.assertEqual(state["exit_only_abort"],"white")
            self.assertEqual(state["rejected_control"],"67")

    def test_controls_are_one_step_only_and_dtr_rts_stay_off(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            control=root/"control"
            status=root/"status.json"
            class FakeSerial:
                in_waiting=1
                def __init__(self):
                    self.writes=[]
                    self.samples=iter(((b"AB gate=white g=next q=abort\n",b"g"),
                                      (b"AB gate=marker g=next q=abort\n",b"gg"),
                                      (b"AB gate=finished g=next q=abort\n",b"q"),
                                      (b"AB end err=0 fast_count=10 healthy=1\nuser@sol:/ ",b"")))
                def open(self):
                    self.asserted=(self.dtr,self.rts,self.port)
                def write(self,data): self.writes.append(data)
                def read(self,n):
                    data,key=next(self.samples)
                    if key:
                        with control.open("ab") as f: f.write(key)
                    return data
                def __enter__(self): return self
                def __exit__(self,*args): pass
            serial=FakeSerial()
            module_path=Path(__file__).parent/"hil"/"capture_quality_ab.py"
            spec=importlib.util.spec_from_file_location("capture_ab",module_path)
            module=importlib.util.module_from_spec(spec)
            with patch.dict(sys.modules,serial=types.SimpleNamespace(Serial=lambda:serial)):
                spec.loader.exec_module(module)
            argv=["capture","--port","COM39","--batch","B","--waveform","dose",
                  "--log",str(root/"capture.log"),"--control",str(control),"--status",str(status)]
            with patch.object(sys,"argv",argv), patch.object(module.time,"sleep",lambda _:None):
                module.main()
            self.assertEqual(serial.asserted,(False,False,"COM39"))
            self.assertEqual(serial.writes,[b"display test display0 ab-b dose\r",b"g",b"q"])
            self.assertTrue(json.loads(status.read_text())["done"])
            self.assertEqual(json.loads(status.read_text())["rejected_control"],"6767")


if __name__=="__main__": unittest.main()
