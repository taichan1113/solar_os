"""Candidate selection only: 40ms Writer ticks and active/latest-pending executor.

This deterministic timing model is not a firmware or optical measurement.
Use the real Writer raster fixture and display_async_test to validate separately.
"""
import json


def simulate(period=0, idle=80, input_times=None, drive=112):
    inputs = input_times if input_times is not None else [60*i for i in range(9)]
    dirty, leading = False, True
    last_edit = last_render = -10000
    count = 0
    submitted, completed = [], []
    active = pending = None
    for t in range(max(inputs)+period+idle+drive*3+81):
        if active and t >= active[0]:
            completed.append(active)
            active = None
        if t in inputs:
            leading = leading or t-last_edit >= idle
            last_edit, count, dirty = t, count+1, True
        if t and t % 40 == 0 and dirty and (not period or leading or
                t-last_render >= period or t-last_edit >= idle):
            pending = count
            submitted.append((t, count))
            last_render, dirty, leading = t, False, False
        if active is None and pending is not None:
            active, pending = (t+drive, pending), None
    latency = [next(t for t,n in completed if n >= i+1)-sent
               for i,sent in enumerate(inputs)]
    return dict(period_ms=period, idle_ms=idle, input_times_ms=inputs,
                renders=len(submitted), physical=len(completed),
                replaced=len(submitted)-len(completed), first_ms=latency[0],
                maximum_ms=max(latency), final_ms=latency[-1],
                max_completion_gap_ms=max((b[0]-a[0] for a,b in
                    zip(completed,completed[1:])),default=0),
                latency_ms=latency, submitted=submitted, completed=completed)


if __name__ == '__main__':
    print(json.dumps([simulate(p) for p in (0,120,160,200,240)], indent=2))
