"""Operator-gated USB capture for one bounded fixed-black condition."""
import argparse
import json
import re
import time
from pathlib import Path

import serial


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--condition", choices=("DAD",), default="DAD")
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--control", type=Path, required=True)
    parser.add_argument("--status", type=Path, required=True)
    args = parser.parse_args()
    if any(p.exists() for p in (args.log, args.control, args.status)):
        raise SystemExit("Use fresh log, control, and status paths")

    state = {"condition": args.condition, "gate": None, "fault": False,
             "ended": False, "done": False, "completed": 0, "markers": [], "events": []}

    def save():
        args.status.write_text(json.dumps(state, indent=2), encoding="utf-8")

    port = serial.Serial()
    port.port, port.baudrate = args.port, 115200
    port.timeout, port.write_timeout = 0, 1
    port.dtr = port.rts = False
    port.open()
    control_offset = 0
    buffer = b""
    with port, args.log.open("xb") as log:
        port.write(f"display test display0 black-hold-dad\r".encode("ascii"))
        save()
        deadline = time.monotonic() + 1800
        while time.monotonic() < deadline:
            data = port.read(port.in_waiting or 1)
            if data:
                log.write(data)
                log.flush()
                buffer += data
                while b"\n" in buffer:
                    raw, buffer = buffer.split(b"\n", 1)
                    line = raw.decode("utf-8", "replace")
                    if line.startswith(("BH phase=", "BH timing ", "BH lut ", "BH mark=", "BH trace ", "BH request ", "BH schedule ", "BH interval ", "BH progress ")):
                        state["events"].append({"line":line.strip(),"received_unix_ns":time.time_ns(),
                                                "received_monotonic_ns":time.monotonic_ns()})
                    marker = re.search(r"BH mark=(\S+)", line)
                    if marker:
                        state["markers"].append(line.strip())
                        state["last_marker"] = marker.group(1)
                    gate = re.search(r"BH gate=(\S+)", line)
                    if gate:
                        state["gate"] = gate.group(1)
                    if line.startswith("BH progress "):
                        count = re.search(r"\bcompleted=(\d+)", line)
                        if count: state["completed"] = int(count.group(1))
                    if line.startswith("BH step="):
                        step = re.search(r"BH step=(\d+)", line)
                        if step:
                            state["completed"] = int(step.group(1))
                    if "BH aborted wait=" in line:
                        state["aborted"] = True
                    if "BH phase=" in line and not re.search(r"\berr=0\b", line):
                        state["fault"] = True
                        state["fault_line"] = line.strip()
                    if "BH rejected:" in line or "BH claim err=" in line or "usage (USB serial):" in line:
                        state["fault"] = True
                        state["fault_line"] = line.strip()
                        state["ended"] = True
                    if "BH end " in line:
                        state["ended"] = True
                        for field in ("completed", "boot_attempts", "diagnostic_cmd20"):
                            value = re.search(r"\b"+field+r"=(\d+)", line)
                            if value: state[field] = int(value.group(1))
                        end_error = re.search(r"\berr=(-?\d+)", line)
                        if end_error and int(end_error.group(1)) != 0:
                            state["fault"] = True
                            state["fault_line"] = line.strip()
                    if state["ended"] and "user@sol:/" in line:
                        state["done"] = True
                if state["ended"] and b"user@sol:/" in buffer:
                    state["done"] = True
                save()
            if args.control.exists():
                control = args.control.read_bytes()
                if len(control) > control_offset:
                    new = control[control_offset:]
                    control_offset = len(control)
                    if new == b"q" or (new == b"g" and state["gate"] and not state["fault"]):
                        port.write(new)
                        state["last_control"] = new.decode("ascii")
                        state["last_gate"] = state["gate"]
                        state["gate"] = None
                    else:
                        state["rejected_control"] = new.hex()
                    save()
            if state["done"]:
                return
            time.sleep(0.01)
        state["capture_timeout"] = True
        save()
        raise SystemExit("Capture timed out; leave current panel state unchanged")


if __name__ == "__main__":
    main()
