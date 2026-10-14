"""One bounded AB batch; exclude setup, cleanup and shell frames from speed stats.

These records prove software completion/invariants, never optical quality or DC
balance. Driver timings are wall time. Small-sample p95 remains exploratory.
"""
import argparse
import json
import re
from pathlib import Path

try:
    from .analyze_fastest_quality import analyze as analyze_ref, stats
except ImportError:
    from analyze_fastest_quality import analyze as analyze_ref, stats


def fields(line):
    return {k: v.rstrip(":") for k, v in re.findall(r"([\w]+)=([^\s]+)", line)}


def analyze(text):
    begins = re.findall(r"AB begin [^\r\n]*", text)
    if len(begins) != 1:
        raise ValueError("exactly one AB begin is required")
    meta = fields(begins[0])
    if meta.get("batch") not in ("A", "B") or meta.get("waveform") not in ("baseline", "dose", "fast6", "fast10"):
        raise ValueError("unknown batch/waveform")
    problems, rows, invariants = [], [], []
    for key, expected in (("width", "800"), ("height", "480"), ("spi_khz", "40000"),
                          ("path", "tile"), ("max_fast", "10")):
        if meta.get(key) != expected:
            problems.append("unexpected metadata: " + key)
    pending, segment = None, []
    finished = ended = cleanup_ok = False
    for line in text.splitlines():
        m = re.search(r"AB (submit|complete|invariant|finished|end)\b.*", line)
        if m:
            event = m[1]
            data = fields(m[0])
            if event == "submit":
                if pending is not None:
                    problems.append("submit without previous completion")
                pending, segment = data, []
            elif event == "complete":
                try:
                    numeric = {k: int(data[k]) for k in ("ordinal", "err", "generation", "completed",
                        "total_us", "busy_us", "convert_us", "spi_us")}
                    if pending is None or any(pending[k] != data[k] for k in ("phase", "ordinal")):
                        raise ValueError("completion does not match submit")
                    row = dict(numeric, phase=data["phase"],
                               changed_pixels=int(pending["changed_pixels"]), cold=int(pending["cold"]))
                    if row["err"] or row["generation"] != row["completed"]:
                        problems.append("failed or uncompleted submission")
                    if row["total_us"] < 0 or not 0 <= row["busy_us"] <= row["total_us"]:
                        problems.append("invalid duration")
                    if row["phase"] in ("initial-fast", "marker"):
                        interval = {"fast6": 6, "fast10": 10}.get(meta["waveform"])
                        expected_waveform = ("official-fast" if interval and row["ordinal"] % interval == 0
                                             else "baseline" if interval else meta["waveform"])
                        ref = analyze_ref("\n".join(segment))
                        row["ref"] = ref
                        raw = "\n".join(segment)
                        phase_info = re.search(r"REF phases ([^\r\n]*)", raw)
                        labels = fields(phase_info[1]) if phase_info else {}
                        if (labels.get("waveform") != expected_waveform or
                            labels.get("spi_khz") != "40000" or labels.get("path") != "tile"):
                            problems.append("cold/warm REF metadata mismatch")
                        bounds = re.search(r"REF err=[^\r\n]*region=(\d+,\d+,\d+,\d+)", raw)
                        row["region"] = bounds[1] if bounds else None
                        if (ref["records"] != 1 or ref["cold"] != row["cold"] or ref["errors"] or
                            ref["incomplete"] or ref["malformed"]):
                            problems.append("missing/invalid physical REF record")
                        for group in ref["driver_groups"]:
                            if (group["waveform"] != expected_waveform or
                                group["spi_khz"] != 40000 or group["path"] != "tile"):
                                problems.append("waveform/clock/path mismatch")
                            row["region"] = group["region"]
                            for key in ("total_us", "busy_us", "convert_us", "spi_us"):
                                if group[key]["n"] != 1 or group[key]["p50"] != row[key]:
                                    problems.append("AB/REF timing mismatch: " + key)
                    if row["phase"] == "exit-cleanup":
                        cleanup_ok = row["err"] == 0
                    rows.append(row)
                except (KeyError, ValueError) as error:
                    problems.append(str(error))
                pending = None
            elif event == "invariant":
                try:
                    v = {k: int(data[k]) for k in ("step", "fixed_black_00", "fixed_white_11",
                        "fixed_bits", "outside_changes", "changed_pixels")}
                    invariants.append(v)
                    if (v["fixed_black_00"] != 1 or v["fixed_white_11"] != 1 or
                        v["fixed_bits"] <= 0 or v["outside_changes"] or v["changed_pixels"] != 512):
                        problems.append("fixed ROI / changed pixels invariant failed")
                except (KeyError, ValueError) as error:
                    problems.append(str(error))
            elif event == "finished":
                finished = data.get("fast_count") == "10"
            elif event == "end":
                ended = data.get("err") == "0" and data.get("fast_count") == "10" and data.get("healthy") == "1"
        elif pending is not None:
            segment.append(line)
    fast = [r for r in rows if r["phase"] in ("initial-fast", "marker")]
    markers = [r for r in fast if r["phase"] == "marker"]
    warm = [r for r in fast if not r["cold"] and not r["err"]]
    expected = 9 if meta["batch"] == "A" else 10
    expected_phases = (["white", "initial-fast"] + ["marker"] * 9 + ["exit-cleanup"]
                       if meta["batch"] == "A" else
                       ["white", "initial-otp"] + ["marker"] * 10 + ["exit-cleanup"])
    if [r["phase"] for r in rows] != expected_phases:
        problems.append("setup/initial/marker/cleanup sequence mismatch")
    if sum(r["cold"] for r in fast) != 1 or len(warm) != 9:
        problems.append("expected one cold and nine warm updates")
    if pending is not None: problems.append("incomplete submission")
    if len(fast) != 10 or [r["ordinal"] for r in fast] != list(range(1, 11)):
        problems.append("expected exactly 10 ordered fast updates")
    if len(markers) != expected or len(invariants) != expected:
        problems.append("marker / invariant count mismatch")
    if [v["step"] for v in invariants] != list(range(1, expected + 1)):
        problems.append("invalid marker step sequence")
    if any(r["changed_pixels"] != 512 for r in markers):
        problems.append("marker changed-pixel mismatch")
    if len({r.get("region") for r in warm}) != 1 or not warm:
        problems.append("unlike/missing warm dirty bounds")
    if not all((finished, ended, cleanup_ok)):
        problems.append("batch not finished or cleanup not completed")
    roi = re.search(r"AB ROI ([^\r\n]*)", text)
    image = re.search(r"AB image ([^\r\n]*)", text)
    result = dict(metadata=meta, roi=fields(roi[1]) if roi else None,
                  image=fields(image[1]) if image else None,
                  valid=not problems, problems=problems, updates=rows,
                  invariants=invariants, cold=sum(r["cold"] for r in fast), warm=len(warm),
                  optical_quantitative="not measured by serial logs", p95="exploratory, n<20")
    result["warm_stats"] = {k: stats([r[k] for r in warm]) for k in
                            ("total_us", "busy_us", "convert_us", "spi_us")}
    result["recovery"] = [r for r in fast if any(g.get("waveform") == "official-fast"
                                                    for g in r.get("ref", {}).get("driver_groups", []))]
    if roi is None:
        result["valid"] = False
        result["problems"].append("missing ROI metadata")
    if image is None:
        result["valid"] = False
        result["problems"].append("missing image identity")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    result = analyze(args.log.read_text(encoding="utf-8", errors="replace"))
    rendered = json.dumps(result, indent=2)
    if args.json:
        args.json.write_text(rendered, encoding="utf-8")
    print(rendered)
    if not result["valid"]:
        raise SystemExit("Not complete acceptance evidence")


if __name__ == "__main__":
    main()
