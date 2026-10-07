"""Summarize opt-in Writer/SSD1677 pipeline captures (monotonic microseconds).

Usage: python scripts/analyze_display_pipeline.py capture.log [...] --csv frames.csv
Deduplicates the ESP/CDC copies of the same diagnostic. Frame IDs are local to
a Writer instance; timestamp containment joins synchronous display/driver spans.
SPI durations are wall time inside polling transmit, not measured wire time.
"""

import argparse
import csv
import re
import statistics
from pathlib import Path


def analyze_ticks(text):
    ticks = {}
    for match in re.finditer(r"frame=\d+ TICK start=\d+ end=\d+ doc_us=\d+ recovery_us=\d+ recovery_err=-?\d+", text):
        values = {k: int(v) for k, v in re.findall(r"(\w+)=(-?\d+)", match[0])}
        ticks[(values["start"], values["end"])] = values
    return sorted(ticks.values(), key=lambda t: t["start"])


def analyze(text):
    seen = set()
    events, refreshes, ticks, reports = [], [], [], []
    for line in text.splitlines():
        match = re.search(r"(?:frame=\d+ (?:WRITER_|MUTATION_|PRESENT_|TICK)|"
                          r"display=\S+ PRESENT_|KEY_REPORT|OTP (?:fast|full)|"
                          r"OTP (?:write|activate|phases)\d+).*", line)
        if not match or match[0] in seen:
            continue
        message = match[0]
        seen.add(message)
        values = {k: int(v) for k, v in re.findall(r"(\w+)=(-?\d+)(?![\w.])", message)}
        if "KEY_REPORT" in message:
            reports.append(values)
        elif " TICK " in message:
            ticks.append(values)
        elif message.startswith("OTP fast") or message.startswith("OTP full"):
            values.update(mode="fast" if "OTP fast" in message else "full",
                          conversion_us=0, spi_us=0, activation_us=0)
            refreshes.append(values)
        elif message.startswith("OTP "):
            if refreshes:
                if "OTP phases" in message:
                    refreshes[-1]["conversion_us"] += values.get("convert_us", 0)
                    refreshes[-1]["spi_us"] += values.get("spi_us", 0)
                elif "OTP activate" in message:
                    refreshes[-1]["activation_us"] += values.get("us", 0)
        else:
            event = re.search(r"\b(WRITER_INPUT|MUTATION_END|WRITER_RENDER_START|"
                              r"WRITER_RENDER_END|PRESENT_RETURN|PRESENT_REQUEST|"
                              r"PRESENT_START|PRESENT_END)\b", message)
            if event:
                events.append(dict(values, event=event[0]))

    events.sort(key=lambda e: e.get("t", 0))
    reports.sort(key=lambda e: e["t"])
    # Queue key reports by character, preserving order even when reception
    # continues during the preceding panel update. Unmatched/synthetic keys
    # remain unknown instead of being assigned a fabricated radio timestamp.
    inputs = [e for e in events if e["event"] == "WRITER_INPUT"]
    used = set()
    for inp in inputs:
        for i, report in enumerate(reports):
            if i not in used and report["key"] == inp["key"] and report["t"] <= inp["t"]:
                inp["report_t"] = report["t"]
                used.add(i)
                break

    rows = []
    previous_end = -1
    for start in (e for e in events if e["event"] == "WRITER_RENDER_START"):
        frame = start["frame"]
        ends = [e for e in events if e["event"] == "PRESENT_RETURN" and
                e.get("frame") == frame and e["t"] >= start["t"]]
        if not ends:
            continue  # incomplete captures are not successful measurements
        end = ends[0]["t"]
        span = [e for e in events if start["t"] <= e["t"] <= end]
        def stage(name):
            return next((e for e in span if e["event"] == name), None)
        render_end, request, present = (stage(n) for n in
                                        ("WRITER_RENDER_END", "PRESENT_REQUEST", "PRESENT_START"))
        if not all((render_end, request, present)):
            continue
        batch = [i for i in inputs if i.get("frame") == frame and previous_end < i["t"] <= start["t"]]
        driver = next((r for r in refreshes if present["t"] <= r["start"] and r["end"] <= end), None)
        tick = next((t for t in ticks if t["start"] <= start["t"] and t["end"] >= end), {})
        first = batch[0]["t"] if batch else None
        last = batch[-1]["t"] if batch else None
        first_report = min((i["report_t"] for i in batch if "report_t" in i), default=None)
        last_report = max((i["report_t"] for i in batch if "report_t" in i), default=None)
        row = dict(frame=frame, start_us=start["t"], end_us=end, inputs=len(batch),
                   keys=" ".join(str(i["key"]) for i in batch),
                   first_input_us=first, last_input_us=last,
                   first_report_us=first_report, last_report_us=last_report,
                   report_to_input_us=first-first_report if first is not None and first_report is not None else None,
                   input_to_render_us=start["t"]-first if first is not None else None,
                   render_us=render_end["t"]-start["t"], clear_us=render_end.get("clear_us"),
                   document_us=tick.get("doc_us", 0), recovery_us=tick.get("recovery_us", 0),
                   recovery_err=tick.get("recovery_err", 0),
                   scheduling_us=present["t"]-request["t"],
                   conversion_us=driver["conversion_us"] if driver else None,
                   spi_us=driver["spi_us"] if driver else None,
                   activation_us=driver["activation_us"] if driver else None,
                   refresh_us=driver["total_us"] if driver else 0,
                   refresh_mode=driver["mode"] if driver else "skipped",
                   refresh_error=driver.get("err", 0) if driver else 0,
                   input_to_complete_us=end-first if first is not None else None,
                   last_input_to_complete_us=end-last if last is not None else None,
                   report_to_complete_us=end-first_report if first_report is not None else None)
        rows.append(row)
        previous_end = end
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", type=Path)
    parser.add_argument("--csv", type=Path, required=True)
    parser.add_argument("--ticks-csv", type=Path)
    args = parser.parse_args()
    rows, ticks = [], []
    for path in args.logs:
        text = path.read_text(encoding="utf-8", errors="replace")
        rows.extend(dict(file=path.name, **r) for r in analyze(text))
        ticks.extend(dict(file=path.name, **t) for t in analyze_ticks(text))
    if not rows:
        parser.error("no complete instrumented Writer frames found")
    with args.csv.open("w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
    print(f"frames={len(rows)} inputs={sum(r['inputs'] for r in rows)} "
          f"refreshes={sum(r['refresh_mode'] != 'skipped' for r in rows)}")
    if ticks:
        print(f"slow_ticks={len(ticks)} max_tick_us={max(t['end']-t['start'] for t in ticks)} "
              f"recovery_ticks={sum(t['recovery_us'] > 0 for t in ticks)} "
              f"max_recovery_us={max(t['recovery_us'] for t in ticks)}")
        if args.ticks_csv:
            with args.ticks_csv.open("w", newline="", encoding="utf-8") as out:
                writer = csv.DictWriter(out, fieldnames=ticks[0].keys())
                writer.writeheader()
                writer.writerows(ticks)
    for key in ("input_to_render_us", "render_us", "clear_us", "document_us", "recovery_us",
                "scheduling_us", "conversion_us", "spi_us", "activation_us", "refresh_us",
                "report_to_input_us", "input_to_complete_us", "report_to_complete_us"):
        values = [r[key] for r in rows if r[key] is not None]
        if values:
            print(f"{key}: min/median/max={min(values)}/{statistics.median(values)}/{max(values)}")


if __name__ == "__main__":
    main()
