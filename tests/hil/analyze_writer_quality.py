"""Validate Writer input/render/completion mapping; never infer optical density."""
import argparse
import json
import re
import statistics
from pathlib import Path


def fields(line):
    result = {}
    for key, value in re.findall(r"(\w+)=(\S+)", line):
        result[key] = int(value) if re.fullmatch(r"-?\d+", value) and key not in ("hash","black") else value
    return result


def analyze(text):
    # Recover row boundaries for diagnostics only when a truncated printf lost
    # its newline. Missing fields still invalidate the capture; never invent them.
    journal = re.sub(r"(?<!\n)(WQ \w+ )", r"\n\1", text)
    rows = {kind: [fields(line) for line in journal.splitlines() if f"WQ {kind} " in line]
            for kind in ("begin", "protocol", "roi", "point", "mask", "scope", "input", "frame", "physical", "observation", "summary", "exit")}
    errors = []
    def check(ok, message):
        if not ok:
            errors.append(message)
    check(len(rows["begin"]) == 1 and len(rows["summary"]) == 1 and len(rows["exit"]) == 1,
          "missing or multiple batch boundaries")
    check(bool(rows["exit"]) and rows["exit"][-1].get("err")==0 and
          rows["exit"][-1].get("healthy")==1 and rows["exit"][-1].get("restored")==1,
          "normal restoration not verified")
    if not rows["summary"] or not rows["begin"]:
        return dict(valid=False, errors=errors, rows=rows, optical_C="未測定", optical_R="未測定")
    summary, begin = rows["summary"][0], rows["begin"][0]
    optical=bool(rows['protocol']) and rows['protocol'][0].get('continuous')==1
    inputs, frames, physical = rows["input"], rows["frame"], rows["physical"]
    check(len(inputs) == summary.get("inputs") == 10, "incomplete input text")
    check(len(frames) == summary.get("renders") == summary.get("submitted"), "render/submit count mismatch")
    check(len(physical) == summary.get("physical") == summary.get("completed"), "physical/completion count mismatch")
    check(len(physical) <= 10 and len(frames) <= 10 and not summary.get("overflow"), "update budget violated")
    check([r.get("seq") for r in inputs] == list(range(1,11)), "input sequence mismatch")
    check([r.get("ch") for r in inputs] == list(b"abcdefghij"), "input text mismatch")
    for r in inputs:
        check(r.get("sent",0) <= r.get("accepted",0) <= r.get("mutated",0), "input timing order")
    by_hash = {r.get("hash"): r for r in frames}
    check(len(by_hash) == len(frames), "ambiguous snapshot identity")
    completed = set()
    for r in physical:
        check(all(k in r for k in ("total_us", "observer_us", "busy_us", "convert_us", "spi_us",
                                  "writes", "activations", "spi_hz", "path")),
              "truncated physical timing/transfer record")
        f = by_hash.get(r.get("hash"), {})
        check(r.get("gen") == f.get("gen") and r.get("chars") == f.get("chars"), "snapshot/generation mapping")
        check(not r.get("err") and r.get("writes") == 2 and r.get("activations") == 1, "driver execution failed")
        check(r.get("spi_hz")==40000000 and r.get("path")=="tile", "SPI/tile condition changed")
        check(r.get("fixed_change") == 0 and r.get("fixed00",0) > 0 and r.get("fixed11",0) > 0, "fixed ROI transitions")
        check(r.get("end",0) >= r.get("start",0), "completion before start")
        completed.add(r.get("gen"))
    if physical:
        check(physical[-1].get("chars") == 10, "final completed snapshot lacks text")
        first_black = physical[0].get("first_change",0)
        check(first_black > 0, "first character ROI empty")
        for r in physical[1:]:
            check(r.get("first_change") == 0 and r.get("first00") == first_black, "first character changed after completion")
            check(r.get("fixed00") == physical[0].get("fixed00") and
                  r.get("fixed11") == physical[0].get("fixed11"), "fixed ROI mask changed")
    replaced = len(frames) - len(completed)
    check(replaced == summary.get("replaced"), "pending replacement accounting")
    first_input_time = {r["seq"]: r["sent"] for r in inputs}
    if begin.get("mode") == "slow":
        for a,b in zip(physical,physical[1:]):
            check(first_input_time.get(b.get("chars"),0) >= a.get("end",0), "slow completion gate violated")
    intervals = [b["start"]-a["start"] for a,b in zip(physical,physical[1:])]
    burst_updates = [b["start"]-a["start"] for a,b in zip(physical,physical[1:])
                     if any(lo<=a.get("chars",0)<=b.get("chars",0)<=hi
                            for lo,hi in (((2,10),) if optical else ((2,4),(5,7),(8,10))))]
    # Gate pauses are reported separately; within-burst timing excludes them.
    burst_input_intervals = [inputs[i]["sent"]-inputs[i-1]["sent"] for i in (range(2,10) if optical else (2,3,5,6,8,9))
                             if len(inputs)>i]
    if begin.get("mode") == "fast":
        check(all(t>=begin.get("fast_interval_ms",60)*1000 for t in burst_input_intervals), "fast input interval below specified rate")
    masks={}
    if optical:
        check(not any(r.get('stage') in ('plus3','plus6') for r in rows['observation']), 'intermediate optical observation gate')
        check([r.get('name') for r in rows['point']]==['T0','T1'],'missing or duplicate T0/T1')
        for point,r in zip(rows['point'],(physical[0],physical[-1]) if physical else ()):
            check(point.get('gen')==r.get('gen') and point.get('completed_us')==r.get('end') and
                  point.get('ready_us',0)-point.get('completed_us',0)>=2000000,'optical point completion/settle mismatch')
        for which in ('fixed','first'):
            try:
                roi=list(map(int,rows['roi'][0][which].split(',')))
                x,y,w,h=roi
                if not (0<=x<x+w<=800 and 0<=y<y+h<=480): raise ValueError('invalid ROI')
                selected=[r for r in rows['mask'] if r.get('which')==which]
                check([r.get('row') for r in selected]==list(range(h)), 'missing or duplicate black-mask row')
                decoded=[bytes.fromhex(str(r['black'])) for r in selected]
                check(all(len(r)==(w+7)//8 for r in decoded), 'black-mask row length')
                count=sum(sum(b.bit_count() for b in r) for r in decoded)
                expected=physical[0]['first_change'] if which=='first' else physical[0]['fixed00']
                check(count==expected,'black-mask count mismatch')
                check(0<=x<x+w<=800 and 0<=y<y+h<=480,'black-mask ROI out of bounds')
                masks[which]=dict(roi=roi,rows=[r['black'] for r in selected],black_pixels=count)
            except (IndexError,KeyError,ValueError,TypeError):
                check(False,'invalid optical black masks')
    return dict(valid=not errors, errors=errors, waveform=begin.get("waveform"), mode=begin.get("mode"),
                input_count=len(inputs), render_count=len(frames), physical_count=len(physical),
                replaced=replaced, first_following_updates=max(0,len(physical)-1),
                mean_update_interval_us=statistics.mean(intervals) if intervals else None,
                intervals_us=intervals, burst_input_intervals_us=burst_input_intervals,
                burst_update_intervals_us=burst_updates,
                mean_burst_update_interval_us=statistics.mean(burst_updates) if burst_updates else None,
                driver_p50_us=statistics.median(r["total_us"] for r in physical)
                    if physical and all("total_us" in r for r in physical) else None,
                observer_p50_us=statistics.median(r["observer_us"] for r in physical)
                    if physical and all("observer_us" in r for r in physical) else None,
                observations=rows["observation"], protocol="continuous-optical" if optical else "stepped",
                black_masks=masks,optical_C="未測定", optical_R="未測定", rows=rows)


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("log", type=Path)
    p.add_argument("--output", type=Path)
    a = p.parse_args()
    result = analyze(a.log.read_text(encoding="utf-8", errors="replace"))
    encoded = json.dumps(result, ensure_ascii=False, indent=2)
    if a.output:
        a.output.write_text(encoded, encoding="utf-8")
    print(encoded)
    raise SystemExit(0 if result["valid"] else 1)
