"""Find recorded 880/1100Hz PC cues without changing video or display.

Candidate detection is not clock calibration. Multiple/missing cues require
manual review; never choose the pair producing a desirable optical result.
"""
import argparse
import array
import json
import math
from pathlib import Path
import subprocess
import sys

from measure_writer_video import ffmpeg_path


def find_cues(samples, rate=8000):
    size=round(rate*.04);step=round(rate*.01)
    tones={name:frequency for name,frequency in (('T0',880),('T1',1100))}
    kernels={name:([math.cos(2*math.pi*f*i/rate) for i in range(size)],
                   [math.sin(2*math.pi*f*i/rate) for i in range(size)]) for name,f in tones.items()}
    detections={name:[] for name in tones}
    for offset in range(0,len(samples)-size+1,step):
        block=samples[offset:offset+size];mean=sum(block)/size
        centered=[v-mean for v in block];energy=sum(v*v for v in centered)
        if energy<size*4:continue
        strengths={name:2*(sum(v*c for v,c in zip(centered,co))**2+
                           sum(v*s for v,s in zip(centered,si))**2)/(size*energy)
                   for name,(co,si) in kernels.items()}
        for name,strength in strengths.items():
            other='T1' if name=='T0' else 'T0'
            if strength>=.35 and strength>strengths[other]*2:
                detections[name].append((offset/rate,strength))
    output=[]
    for name,rows in detections.items():
        groups=[]
        for row in rows:
            if not groups or row[0]-groups[-1][-1][0]>.021:groups.append([])
            groups[-1].append(row)
        for group in groups:
            duration=group[-1][0]-group[0][0]+.04
            if .10<=duration<=.40:
                output.append(dict(point=name,video_time_s=group[0][0],
                    uncertainty_s=.06,detected_span_s=duration,
                    concentration_peak=max(r[1] for r in group)))
    return sorted(output,key=lambda row:row['video_time_s'])


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--video',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--ffmpeg')
    a=p.parse_args()
    if a.output.exists():p.error('output already exists; use a new path')
    run=subprocess.run([ffmpeg_path(a.ffmpeg),'-hide_banner','-nostdin','-i',str(a.video),
        '-map','0:a:0','-vn','-ac','1','-ar','8000','-f','s16le','pipe:1'],capture_output=True)
    if run.returncode:raise ValueError('audio unavailable; no synchronization guessed')
    samples=array.array('h');samples.frombytes(run.stdout)
    if sys.byteorder!='little':samples.byteswap()
    candidates=find_cues(samples)
    valid=len(candidates)==2 and [r['point'] for r in candidates]==['T0','T1']
    result=dict(method='recorded PC 880/1100Hz cues; threshold onset candidates require review',
        candidates=candidates,unique_ordered_pair=valid,
        markers=candidates if valid else [],uart_and_cue_delay_bound_s=None,
        caveat='60ms is audio detector resolution allowance, not a calibrated bound. UART/beep delay remains unmeasured.')
    a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
    return 0 if valid else 1


if __name__=='__main__':raise SystemExit(main())
