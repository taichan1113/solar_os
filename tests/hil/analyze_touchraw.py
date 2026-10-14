"""Validate and summarize stopped USB touchraw dumps, without device access."""
import argparse
import gzip
from collections import Counter
import json
import math
from pathlib import Path
import re
import statistics


def fields(line):
    return dict(item.split("=", 1) for item in line.split() if "=" in item)


def contact(raw, slot, count):
    p = raw[1 + slot * 6:7 + slot * 6]
    ident, event = p[2] >> 4, p[0] >> 6
    return [ident, ((p[0] & 15) << 8) | p[1],
            ((p[2] & 15) << 8) | p[3], event,
            int(slot < count), int(slot < count and ident != 15 and event != 3)]


def timing(values):
    if not values:
        return {"samples": 0, "p50_us": None, "p95_us": None, "max_us": None}
    ordered = sorted(values)
    return {"samples": len(values), "p50_us": statistics.median(values),
            "p95_us": ordered[math.ceil(len(values) * .95) - 1], "max_us": max(values),
            "p95_sample_warning": len(values) < 20}


def analyze(text):
    # Terminals can surround lines with ANSI cursor/redraw sequences.
    text = re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", text)
    rows = [fields(m) for m in re.findall(r"TOUCHRAW sample ([^\r\n]*)", text)]
    endings = re.findall(r"TOUCHRAW end ([^\r\n]*)", text)
    if len(endings) != 1:
        raise ValueError("exactly one complete dump end marker required")
    end = {k: int(v) for k, v in fields(endings[0]).items()}
    if len(rows) != end["stored"] or len(rows) != end["emitted"]:
        raise ValueError("dump is incomplete or contains duplicate samples")
    statuses = re.findall(r"TOUCHRAW status ([^\r\n]*)", text)
    if not statuses:
        raise ValueError("dump status missing")
    status = fields(statuses[-1])
    if int(status["stored"]) != len(rows) or int(status["dropped"]) != end["dropped"]:
        raise ValueError("journal occupancy/footer mismatch")
    if int(status["reads"]) != len(rows) + end["dropped"]:
        raise ValueError("read count does not account for dropped records")
    counts, results, flags, all_flags = Counter(), Counter(), Counter(), Counter()
    ids, unused, up_counts, transitions = Counter(), Counter(), Counter(), []
    all_up_counts = Counter()
    durations, intervals = [], []
    failures = consecutive = max_consecutive = 0
    previous_start = 0
    previous_key = None
    sentinel = reported_sentinel = duplicate = swaps = 0
    prior_ids = None
    coordinates = {}
    for i, row in enumerate(rows, 1):
        r = {k: int(v) for k, v in row.items() if k not in ("raw", "p1", "p2")}
        raw = bytes.fromhex(row["raw"])
        if len(raw) != 13 or r["seq"] != i:
            raise ValueError("raw length/sequence invalid")
        if r["end"] < r["start"] or r["duration"] != r["end"] - r["start"]:
            raise ValueError("read timestamps/duration invalid")
        expected_interval = r["start"] - previous_start if i > 1 else 0
        if r["interval"] != expected_interval or (i > 1 and expected_interval <= 0):
            raise ValueError("sample interval/monotonic order invalid")
        previous_start = r["start"]
        durations.append(r["duration"])
        if i > 1:
            intervals.append(r["interval"])
        if r["error"]:
            if r["measured"] or any(raw) or r["result"] or r["reasons"]:
                raise ValueError("failed transport record contains a measurement")
            failures += 1
            consecutive += 1
            max_consecutive = max(max_consecutive, consecutive)
            continue
        consecutive = 0
        if r["measured"] != 1:
            raise ValueError("successful read not marked measured")
        count = raw[0] & 15
        p = [contact(raw, slot, count) for slot in range(2)]
        reasons = 4 if count > 2 else 0
        for c in p:
            all_flags[c[3]] += 1
            if c[3] == 1:
                all_up_counts[count] += 1
            sentinel += c[0] == 15
            if not c[4]:
                unused[tuple(c[:4])] += 1
                continue
            flags[c[3]] += 1
            reported_sentinel += c[0] == 15
            reasons |= (16 if c[0] == 15 else 0) | (32 if c[3] == 3 else 0) | (64 if c[3] == 1 else 0)
            if c[3] == 1:
                up_counts[count] += 1
            ids[c[0]] += 1
            coordinates.setdefault(c[0], []).append(c[1:3])
        if count >= 2 and p[0][0] == p[1][0]:
            reasons |= 8
            p[0][5] = p[1][5] = 0
            duplicate += 1
        expected_result = 0 if reasons & ~64 else 2 if reasons else 1
        if r["result"] != expected_result or r["reasons"] != reasons:
            raise ValueError("stored decoder classification disagrees with raw")
        for slot in range(2):
            if list(map(int, row[f"p{slot + 1}"].split(","))) != p[slot]:
                raise ValueError("dumped contact disagrees with raw")
        current_ids = tuple(c[0] for c in p if c[4])
        if len(current_ids) == 2 and prior_ids and len(prior_ids) == 2:
            if current_ids == prior_ids[::-1] and current_ids != prior_ids:
                swaps += 1
        prior_ids = current_ids
        key = (count, tuple((c[0], c[3], c[4]) for c in p), r["result"])
        if key != previous_key:
            transitions.append({"sequence": i, "start_us": r["start"], "count": count,
                                "raw": row["raw"], "contacts": p,
                                "result": r["result"], "reasons": reasons})
            previous_key = key
        counts[count] += 1
        results[r["result"]] += 1
    if not end["dropped"] and (failures != int(status["failures"]) or max_consecutive != int(status["max_consecutive"])):
        raise ValueError("journal error counters disagree with samples")
    return {"stored": len(rows), "dropped": end["dropped"], "status": status,
            "counts": dict(counts), "decoder_results": dict(results),
            "reported_flags": dict(flags), "all_slot_flags": dict(all_flags),
            "reported_up_counts": dict(up_counts), "all_slot_up_counts": dict(all_up_counts), "id_histogram": dict(ids),
            "sentinel_all_slots": sentinel, "reported_sentinel": reported_sentinel,
            "duplicate_id_frames": duplicate, "adjacent_two_slot_swaps": swaps,
            "unused_slot_examples": [{"contact": list(k), "samples": v} for k, v in unused.most_common(8)],
            "coordinate_ranges_by_id": {k: {"x": [min(p[0] for p in v), max(p[0] for p in v)],
                                              "y": [min(p[1] for p in v), max(p[1] for p in v)]}
                                        for k, v in coordinates.items()},
            "read_duration": timing(durations), "read_start_interval": timing(intervals),
            "i2c_failures": failures, "max_consecutive_failures": max_consecutive,
            "transitions": transitions,
            "coherence": "raw series requires case-specific review; burst does not prove controller latch"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dump", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.dump.suffix == ".gz":
        with gzip.open(args.dump, "rt", encoding="utf-8", errors="replace") as stream:
            source = stream.read()
    else:
        source = args.dump.read_text(encoding="utf-8", errors="replace")
    result = analyze(source)
    encoded = json.dumps(result, indent=2, ensure_ascii=False)
    if args.output:
        args.output.write_text(encoded + "\n", encoding="utf-8")
    else:
        print(encoded)


if __name__ == "__main__":
    main()
