"""Capture ONE bounded, operator-stepped RAM Writer condition.

Append exactly one g to --control only after the user observes the current gate.
q aborts safely. No automatic progression, flash, file upload or BLE input.
"""
import argparse
import json
import re
import time
from datetime import datetime, timezone
from pathlib import Path


def parse_line(state, line):
    if "WQ point " in line:
        point=dict(re.findall(r"(\w+)=(\S+)",line))
        point['received_utc']=datetime.now(timezone.utc).isoformat()
        state.setdefault('points',{})[point['name']]=point
    gate = re.search(r"WQ gate=(\S+) inputs=(\d+) renders=(\d+) starts=(\d+)", line)
    if gate:
        state.update(gate=gate[1], inputs=int(gate[2]), renders=int(gate[3]), starts=int(gate[4]))
    if (re.search(r"WQ (?:physical|exit) .*err=(?!0\b)-?\d+", line) or
            "WQ rejected:" in line or "WQ claim err=" in line or "usage (serial):" in line):
        state.update(fault=True)
        if "WQ physical " not in line:
            state.update(ended=True, gate=None)
    if "WQ exit " in line:
        state.update(gate=None, ended=True, exit=line)
    if state.get("ended") and "user@sol:/" in line:
        state["done"] = True


def allowed_control(state, command):
    return command == b"q" or (command == b"g" and state.get("gate") in
        {"white", "reference", "first", "plus3", "plus6", "final"} and not state.get("fault"))


def main():
    import serial
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port", required=True)
    p.add_argument("--waveform", choices=("baseline", "dose"), required=True)
    p.add_argument("--mode", choices=("slow", "fast"), required=True)
    p.add_argument("--optical", action="store_true", help="Baseline continuous T0/T1, no intermediate gates")
    p.add_argument("--photo-cue", action="store_true", help="Windows audible cue at completed+2s T0/T1")
    for name in ("log", "control", "status"):
        p.add_argument("--" + name, type=Path, required=True)
    a = p.parse_args()
    if a.optical and a.waveform != "baseline": p.error("optical starts with baseline only")
    if a.photo_cue and not a.optical: p.error("photo cue requires optical mode")
    if a.photo_cue:
        import winsound
    if a.log.exists() or a.control.exists():
        raise SystemExit("use new capture paths")
    port = serial.Serial()
    port.port, port.baudrate, port.timeout = a.port, 115200, 0
    port.dtr = port.rts = False
    port.open()
    state = dict(waveform=a.waveform, mode=a.mode, gate=None, fault=False, done=False)
    offset = 0
    buffered = b""
    with port, a.log.open("xb") as log:
        suffix=" optical" if a.optical else ""
        port.write(f"display test display0 writer-quality {a.waveform} {a.mode}{suffix}\r".encode("ascii"))
        deadline = time.monotonic() + 1800
        while time.monotonic() < deadline:
            data = port.read(port.in_waiting or 1)
            if data:
                log.write(data)
                log.flush()
                buffered += data
                while b"\n" in buffered:
                    raw, buffered = buffered.split(b"\n", 1)
                    line=raw.decode("utf-8", "replace")
                    parse_line(state,line)
                    if a.photo_cue and "WQ point " in line:
                        name="T0" if "name=T0 " in line else "T1"
                        state['points'][name]['cue_utc']=datetime.now(timezone.utc).isoformat()
                        winsound.Beep(880 if name=='T0' else 1100,200)
                if state.get("ended") and b"user@sol:/" in buffered:
                    state["done"] = True
                a.status.write_text(json.dumps(state, indent=2), encoding="utf-8")
            if a.control.exists():
                control = a.control.read_bytes()
                if len(control) > offset:
                    command = control[offset:]
                    offset = len(control)
                    if allowed_control(state, command):
                        port.write(command)
                        state["gate"] = None
                    else:
                        state["rejected_control"] = command.hex()
                    a.status.write_text(json.dumps(state, indent=2), encoding="utf-8")
            if state["done"]:
                return
            time.sleep(.01)
        state["capture_timeout"] = True
        a.status.write_text(json.dumps(state, indent=2), encoding="utf-8")
        raise SystemExit("timeout; no new run or automatic cleanup was sent")


if __name__ == "__main__":
    main()
