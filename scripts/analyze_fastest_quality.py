"""Offline REF/async analysis. One log per waveform/pattern/generation epoch.

Reports warm successful complete REF records, errors and incomplete records
separately. Durations are measured wall time, not panel optics or wire time.
Uses nearest-rank p95; n<20 is exploratory. No hardware access or flashing.
"""
import argparse
import json
import math
import re
import statistics
from pathlib import Path

try:
    from .analyze_display_async import analyze as analyze_async
except ImportError:
    from analyze_display_async import analyze as analyze_async


def stats(values):
    values = sorted(v for v in values if v is not None)
    if not values:
        return None
    return dict(n=len(values), mean=statistics.mean(values),
                p50=statistics.median(values),
                p95=values[math.ceil(len(values) * .95) - 1], max=max(values))


def analyze(text):
    records, current, malformed = {}, None, []
    for line in text.splitlines():
        m = re.search(r"\bREF (err=|phases |write[01] ).*", line)
        if not m:
            continue
        message = m[0]
        numeric = {k: int(v) for k, v in re.findall(r"(\w+)=(-?\d+)", message)}
        if message.startswith("REF err="):
            required = {"err", "cold", "bytes", "start", "end", "total_us", "busy_us"}
            region = re.search(r"region=(\d+,\d+,\d+,\d+)\b", message)
            if not required <= numeric.keys() or not region:
                malformed.append(message)
                current = None
                continue
            key = (numeric["start"], numeric["end"])
            current = records.setdefault(key, dict(numeric, region=region[1], writes={}))
        elif current is not None:
            if "phases" in message:
                labels = {k: v for k, v in re.findall(r"(path|waveform)=(\w+)", message)}
                current.update(numeric, **labels)
            else:
                required = {"us", "convert_us", "spi_us"}
                if not required <= numeric.keys():
                    malformed.append(message)
                else:
                    index = int(re.search(r"REF write([01])", message)[1])
                    current["writes"][index] = numeric
    all_records = sorted(records.values(), key=lambda r: r["start"])
    def complete(r):
        return (r["bytes"] == 96000 and r["end"] >= r["start"] and
                r["total_us"] == r["end"] - r["start"] and
                0 <= r["busy_us"] <= r["total_us"] and
                {0, 1} <= r["writes"].keys() and
                {"path", "spi_khz", "diff_us", "promote_us", "waveform"} <= r.keys())
    eligible = [r for r in all_records if not r["err"] and not r["cold"] and complete(r)]
    groups = {}
    # Never combine unlike waveforms, clocks, paths or dirty bounds into p95.
    for r in eligible:
        key = (r["waveform"], r["spi_khz"], r["path"], r["region"])
        groups.setdefault(key, []).append(r)
    summaries = []
    for (waveform, clock, path, region), rows in groups.items():
        summary = dict(waveform=waveform, spi_khz=clock, path=path, region=region)
        for name in ("total_us", "busy_us", "diff_us", "promote_us"):
            summary[name] = stats([r[name] for r in rows])
        for name in ("convert_us", "spi_us"):
            summary[name] = stats([sum(w[name] for w in r["writes"].values()) for r in rows])
        summaries.append(summary)
    asynchronous = analyze_async(text)
    copies = {}
    for message in re.findall(r"gen=\d+ SNAPSHOT_SUBMIT[^\r\n]*", text):
        values = {k: int(v) for k, v in re.findall(r"(\w+)=(-?\d+)", message)}
        if {"gen", "t", "copied_us"} <= values.keys():
            copies[(values["gen"], values["t"])] = values["copied_us"]
    pipeline = {
        "input_dispatch_us": stats([i.get("dispatch_us") for i in asynchronous["inputs"]]),
        "input_to_complete_us": stats([i.get("catchup_us") for i in asynchronous["inputs"]]),
        "writer_render_us": stats([f["render_us"] for f in asynchronous["frames"]]),
        "submit_us": stats([f["submit_us"] for f in asynchronous["frames"]]),
        "worker_us": stats([w["duration_us"] for w in asynchronous["workers"] if not w["error"] and w["physical"]]),
        "snapshot_copy_us": stats(copies.values()),
    }
    return dict(records=len(all_records), warm_complete=len(eligible),
                errors=[r for r in all_records if r["err"]],
                incomplete=[r for r in all_records if not complete(r)],
                cold=sum(bool(r["cold"]) for r in all_records), malformed=malformed,
                driver_groups=summaries, pipeline=pipeline,
                malformed_async=asynchronous["malformed_events"],
                async_counts=asynchronous["counts"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    result = analyze(args.log.read_text(encoding="utf-8", errors="replace"))
    if args.json:
        args.json.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))
    if not result["warm_complete"] or result["errors"] or result["incomplete"] or result["malformed"] or result["malformed_async"]:
        raise SystemExit("Capture is not valid acceptance evidence; inspect missing/error records.")


if __name__ == "__main__":
    main()
