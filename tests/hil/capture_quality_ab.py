"""Capture ONE operator-stepped serial diagnostic; no flash or storage upload.

After observing the panel, append one g to --control to advance ONE update.
Append q to abort, or to exit the finished batch and run its explicit cleanup.
Never append multiple g characters together or automate unattended batches.
"""
import argparse
import json
import re
import time
from pathlib import Path
import serial


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port", required=True)
    p.add_argument("--batch", choices=("A", "B"), required=True)
    p.add_argument("--waveform", choices=("baseline", "dose", "fast6", "fast10"), required=True)
    p.add_argument("--log", type=Path, required=True)
    p.add_argument("--control", type=Path, required=True)
    p.add_argument("--status", type=Path, required=True)
    p.add_argument("--exit-only", action="store_true",
                   help="abort automatically at white gate; never send g or start a fast update")
    a = p.parse_args()
    if a.control.exists() or a.log.exists():
        raise SystemExit("use new control/log files")
    port = serial.Serial()
    port.port, port.baudrate, port.timeout = a.port, 115200, 0
    port.dtr = port.rts = False
    port.open()
    state = {"batch": a.batch, "waveform": a.waveform, "gate": None, "fault": False, "done": False}
    if a.exit_only:
        state["exit_only"] = True
    offset = 0
    line_buffer = b""
    with port, a.log.open("xb") as log:
        port.write(("display test display0 ab-%s %s\r" %
                    (a.batch.lower(), a.waveform)).encode("ascii"))
        deadline = time.monotonic() + 1800
        while time.monotonic() < deadline:
            data = port.read(port.in_waiting or 1)
            if data:
                log.write(data); log.flush()
                line_buffer += data
                while b"\n" in line_buffer:
                    raw, line_buffer = line_buffer.split(b"\n", 1)
                    line = raw.decode("utf-8", "replace")
                    gate = re.search(r"AB gate=(\S+)", line)
                    if gate:
                        state["gate"] = gate[1]
                        if a.exit_only:
                            # The firmware cannot reach initial black or any
                            # fast update without g. Only abort is permitted.
                            port.write(b"q")
                            state["exit_only_abort"] = gate[1]
                            state["gate"] = None
                            if gate[1] != "white":
                                state["fault"] = True
                    if re.search(r"\b(?:AB complete|REF) .*err=(?!0\b)-?\d+", line):
                        state["fault"] = True
                    if "AB end " in line:
                        state["gate"] = None
                        state["ended"] = True
                    if "AB complete phase=exit-cleanup" in line:
                        state["cleanup"] = line
                    if "AB rejected:" in line or "AB claim error=" in line or "usage (serial shell):" in line:
                        state["fault"] = True
                        state["ended"] = True
                    if state.get("ended") and "user@sol:/" in line:
                        state["done"] = True
                if state.get("ended") and b"user@sol:/" in line_buffer:
                    state["done"] = True
                a.status.write_text(json.dumps(state, indent=2), encoding="utf-8")
            if a.control.exists():
                control = a.control.read_bytes()
                if len(control) > offset:
                    command = control[offset:]
                    offset = len(control)
                    if command == b"q" or (command == b"g" and not a.exit_only and state["gate"] not in (None, "finished") and not state["fault"]):
                        port.write(command)
                        state["gate"] = None
                    else:
                        state["rejected_control"] = command.hex()
                    a.status.write_text(json.dumps(state, indent=2), encoding="utf-8")
            if state["done"]:
                return
            time.sleep(.01)
        # Timeout preserves the current stopped image. Never launch another run.
        state["capture_timeout"] = True
        a.status.write_text(json.dumps(state, indent=2), encoding="utf-8")
        raise SystemExit("capture timed out; inspect device before further actions")


if __name__ == "__main__":
    main()
