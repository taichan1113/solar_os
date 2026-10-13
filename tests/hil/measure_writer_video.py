"""Extract predeclared video times and measure immutable Writer T0 black masks.

FFmpeg + Pillow, no OpenCV required. Original video is read-only. Frames are
temporally correlated, not independent trials or calibrated reflectance.
Recommended camera settings are recorded, never prerequisite checkboxes.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess

import measure_writer_optical as optical


def ffmpeg_path(explicit=None):
    found=explicit or os.environ.get('WRITER_VIDEO_FFMPEG') or shutil.which('ffmpeg')
    if not found:raise ValueError('FFmpeg unavailable; supply --ffmpeg or WRITER_VIDEO_FFMPEG')
    return str(found)


def ffmpeg_run(executable,args):
    process=subprocess.run([executable,'-hide_banner','-nostdin',*map(str,args)],
        stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=False)
    stderr=process.stderr.decode('utf-8','replace')
    if process.returncode:raise ValueError('FFmpeg failed: '+stderr[-3000:])
    return stderr


def probe_video(path,executable):
    # showinfo gives decoded orientation and actual frame PTS. Do not assume
    # stream dimensions or a constant nominal frame rate describe iPhone VFR.
    log=ffmpeg_run(executable,['-i',path,'-map','0:v:0','-vf','showinfo','-frames:v','1','-an','-f','null','-'])
    duration=re.search(r'Duration: (\d+):(\d+):(\d+(?:\.\d+)?)',log)
    size=re.search(r'pts_time:[^\n]*?s:(\d+)x(\d+)',log)
    fps=re.search(r'(\d+(?:\.\d+)?) fps',log)
    if not duration or not size:raise ValueError('video duration or decoded size unavailable')
    h,m,s=duration.groups()
    return dict(duration_s=int(h)*3600+int(m)*60+float(s),
                decoded_size=list(map(int,size.groups())),nominal_fps=float(fps[1]) if fps else None,
                variable_frame_rate_assumed_possible=True,
                decoder_log='\n'.join(line for line in log.splitlines()
                    if not any(key in line.lower() for key in ('location','gps'))))


def sync_windows(analysis,sync,offsets=(.5,1,1.5)):
    points={p['name']:p for p in analysis['rows']['point']}
    if set(points)!={'T0','T1'}:raise ValueError('exactly T0/T1 journal points required')
    markers=sync.get('markers',[])
    if not markers:raise ValueError('video/log correspondence missing')
    known=[]
    for m in markers:
        point=points.get(m.get('point'))
        time=m.get('video_time_s')
        if not point or not isinstance(time,(int,float)) or not math.isfinite(time) or time<0:
            raise ValueError('invalid video marker')
        known.append((point['ready_us']/1e6,time,m.get('uncertainty_s')))
    if len(known)>2 or len({m.get('point') for m in markers})!=len(markers):
        raise ValueError('duplicate or too many sync markers')
    known.sort()
    if len(known)==2:
        if known[1][0]<=known[0][0] or known[1][1]<=known[0][1]:raise ValueError('non-monotonic video/log sync')
        scale=(known[1][1]-known[0][1])/(known[1][0]-known[0][0])
    else:scale=1
    origin=known[0][1]-scale*known[0][0]
    uncertainty=[v for _,_,v in known]
    delay=sync.get('uart_and_cue_delay_bound_s')
    finite=lambda x:isinstance(x,(int,float)) and not isinstance(x,bool) and math.isfinite(x) and x>=0
    bound=max(uncertainty)+delay if all(finite(v) for v in uncertainty) and finite(delay) else None
    if not offsets or any(not math.isfinite(v) or not .5<=v<=2 for v in offsets) or len(set(offsets))!=len(offsets):
        raise ValueError('declare distinct completion offsets within 0.5..2s')
    windows={name:[dict(requested_video_time_s=origin+scale*(p['completed_us']/1e6+d),
                       requested_after_completion_s=d) for d in sorted(offsets)] for name,p in points.items()}
    return windows,dict(method=sync.get('method','unrecorded'),markers=markers,
        video_per_device_second=scale,video_origin_s=origin,time_uncertainty_s=bound,
        caveat='Audio onset selection + UART/beep delay + single-marker clock drift; bounds are declarations, not confidence intervals.')


def extract_frame(path,time_s,out,executable):
    # Decode in order; select first actual presentation timestamp >= target.
    # No nominal-FPS indexing, output upscaling or metric-driven selection.
    if out.exists():raise ValueError('frame output already exists')
    log=ffmpeg_run(executable,['-i',path,'-map','0:v:0','-vf',
        f"select=gte(t\\,{time_s:.9f}),showinfo",'-frames:v','1','-fps_mode','vfr','-an',out])
    match=re.search(r'pts_time:([-\d.eE+]+)',log)
    if not out.is_file() or not match:raise ValueError('requested frame unavailable')
    pts=float(match[1])
    if pts+1e-6<time_s:raise ValueError('decoder returned frame before requested PTS')
    return pts


def frame_sampler(path,corners):
    from PIL import Image
    image=Image.open(path).convert('L')
    if len(corners)!=4 or any(len(p)!=2 for p in corners):raise ValueError('four visible panel corners required')
    coeff=optical.homography(corners)
    def sample(x,y):
        a,b,c,d,e,f,g,h=coeff;den=g*x+h*y+1
        u=(a*x+b*y+c)/den;v=(d*x+e*y+f)/den
        ix=math.floor(u);iy=math.floor(v);dx=u-ix;dy=v-iy
        if not (0<=ix<image.width-1 and 0<=iy<image.height-1):raise ValueError('ROI outside decoded frame')
        return ((1-dx)*(1-dy)*image.getpixel((ix,iy))+dx*(1-dy)*image.getpixel((ix+1,iy))+
                (1-dx)*dy*image.getpixel((ix,iy+1))+dx*dy*image.getpixel((ix+1,iy+1)))
    return sample


def summarize_frames(frames):
    if not frames or {f['point'] for f in frames}!={'T0','T1'}:raise ValueError('T0/T1 decoded frames missing')
    regions={};reasons=[]
    names=set(frames[0]['regions'])
    for name in names:
        stages={}
        for point in ('T0','T1'):
            rows=[f['regions'][name] for f in frames if f['point']==point]
            values=[r['C'] for r in rows]
            W=[r['W'] for r in rows]
            if not values:raise ValueError('empty measurement window')
            if any(not math.isfinite(c) or c<=0 for c in values):reasons.append(name+': non-positive contrast')
            stages[point]=dict(C=statistics.median(values),C_min=min(values),C_max=max(values),
                C_MAD=statistics.median(abs(c-statistics.median(values)) for c in values),
                W=statistics.median(W),W_min=min(W),W_max=max(W),
                white_MAD=statistics.median(r['white_MAD'] for r in rows),frames=len(rows))
        a,b=stages['T0'],stages['T1']
        drop=a['C']-b['C']
        variability=max(a['C_max']-a['C_min'],b['C_max']-b['C_min'])
        white_change=abs(b['W']/a['W']-1) if a['W'] else math.inf
        proxy=2*1.4826*(a['white_MAD']/a['W']+b['white_MAD']/b['W']) if a['W'] and b['W'] else math.inf
        flags=[]
        white_window_variation=max((r['W_max']-r['W_min'])/r['W'] for r in (a,b) if r['W']>0)
        if variability>abs(drop):flags.append('within-window contrast variation exceeds apparent drop')
        if white_window_variation>abs(drop):flags.append('within-window white brightness variation exceeds apparent drop')
        if white_change>abs(drop):flags.append('white brightness change exceeds apparent contrast drop')
        if proxy>abs(drop):flags.append('spatial white-noise proxy exceeds apparent contrast drop')
        regions[name]=dict(T0=a,T1=b,R=b['C']/a['C'] if a['C']>0 else None,
            contrast_drop=drop,white_relative_change=b['W']/a['W']-1 if a['W'] else None,
            white_noise_proxy=proxy,uncertainty_flags=flags)
    return dict(regions=regions,invalid_reasons=reasons,quantitative_verdict='withheld',
        correlated_frames=True,independent_trial_count=1,
        caveat='Frame ranges/MAD and white-noise proxies are not statistical confidence intervals. Exposure/WB/HDR/compression may remain confounded.')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('video','analysis','sync','corners','output-dir'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--ffmpeg')
    p.add_argument('--offsets',default='0.5,1,1.5')
    a=p.parse_args();executable=ffmpeg_path(a.ffmpeg)
    analysis=json.loads(a.analysis.read_text(encoding='utf-8'))
    if not analysis.get('valid') or analysis.get('protocol')!='writer-video':
        raise ValueError('complete validated Writer video journal required')
    if not a.video.is_file():raise ValueError('original video missing; no density estimate')
    if a.output_dir.exists():raise ValueError('use a new output directory; original and prior frames are preserved')
    config=json.loads(a.corners.read_text(encoding='utf-8'));sync=json.loads(a.sync.read_text(encoding='utf-8'))
    windows,mapping=sync_windows(analysis,sync,tuple(map(float,a.offsets.split(','))))
    probe=probe_video(a.video,executable)
    a.output_dir.mkdir(parents=True)
    result=dict(valid=False,source=str(a.video),source_sha256=None,video=probe,synchronization=mapping,
        camera_conditions=config.get('camera_conditions','unrecorded'),frames=[],
        selection_policy='predeclared completion offsets; first actual decoded PTS >= request',
        processing=['FFmpeg default video autorotation/decoding','lossless PNG extraction at decoded resolution',
                    'Pillow grayscale','corner homography sampling','independent OTP reference translation'],
        error=None,uncertainties=[])
    hasher=hashlib.sha256()
    with a.video.open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):hasher.update(block)
    result['source_sha256']=hasher.hexdigest()
    try:
        masks=analysis['black_masks'];reference='fixed'
        for point,requests in windows.items():
            for index,request in enumerate(requests):
                t=request['requested_video_time_s']
                if not 0<=t<probe['duration_s']:raise ValueError('measurement interval outside video')
                out=a.output_dir/f'{point}-{index}.png'
                actual=extract_frame(a.video,t,out,executable)
                if any(f['actual_video_time_s']==actual for f in result['frames']):
                    raise ValueError('distinct requested samples decoded to the same video frame')
                if actual-t>.1:
                    result['uncertainties'].append(f'{out.name}: decoded frame is more than 100ms after request; possible frame gap')
                corners=config.get('frames',{}).get(out.name,config.get(point))
                if not corners:raise ValueError('frame panel corners missing')
                sampler=frame_sampler(out,corners)
                measured,pose=optical.compare_with_reference([sampler,sampler],masks,reference)
                frame=dict(point=point,file=out.name,**request,actual_video_time_s=actual,
                    extraction_delay_s=actual-t,actual_after_completion_s=request['requested_after_completion_s']+(actual-t)/mapping['video_per_device_second'],
                    registration=pose['T0'],regions={})
                for name,r in measured.items():
                    frame['regions'][name]={**r['T0'],'black_pixels':r['black_pixels'],
                        'used_for_registration':r['used_for_registration'],'distance_from_reference_px':r['distance_from_reference_px']}
                result['frames'].append(frame)
        result.update(summarize_frames(result['frames']))
        result['valid']=not result['invalid_reasons']
        result['quantitative_verdict']='withheld; exploratory camera-contrast trend only'
        if mapping['time_uncertainty_s'] is None:result['uncertainties'].append('synchronization uncertainty unmeasured; timing attribution withheld')
    except (ValueError,KeyError,TypeError) as exc:
        result['error']=str(exc);result['valid']=False
    (a.output_dir/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:result.get(k) for k in ('valid','error','quantitative_verdict','invalid_reasons','regions')},ensure_ascii=False,indent=2))
    return 0 if result['valid'] else 1


if __name__=='__main__':raise SystemExit(main())
