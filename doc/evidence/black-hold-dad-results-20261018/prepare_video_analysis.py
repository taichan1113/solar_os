"""Reuse B/D extraction: read originals, locate every repeat using USB receipts."""
from pathlib import Path
import datetime as dt,json,re,subprocess,sys
from PIL import Image

base=Path(__file__).resolve().parent;root=base.parents[1]
run=base/'archive/20261018-dad';out=run/'analysis';out.mkdir(exist_ok=True)
ffmpeg=root/'.research/writer-video-quality/python-deps/imageio_ffmpeg/binaries/ffmpeg-linux-x86_64-v7.0.2'
sys.path.insert(0,str(base/'worktree/tests/hil'))
import measure_writer_video as video
entries=[]
for source_info in json.loads((run/'video-sources.json').read_text(encoding='utf-8-sig')):
    c=source_info['condition'];src=source_info['source']
    source=Path('/mnt/'+src[0].lower()+src[2:].replace(chr(92),'/'))
    measurement=json.loads((run/f'{c}-measurements.json').read_text())
    status=json.loads((run/'dad-status.json').read_text())
    assert status['done'] and not status['fault'] and status['completed']==3
    created=dt.datetime.fromisoformat(source_info['source_created_utc'].replace('Z','+00:00')).timestamp()
    probe=video.probe_video(source,str(ffmpeg));markers={};events=[];samples=[]
    for name in ('T0','T1','T2','T3'):
        event=next(x for x in status['events'] if x['line'].startswith('BH mark='+name+' '))
        markers[name]=event['received_unix_ns']/1e9-created
    for n,p in enumerate(measurement['measured'],1):
        event=next(x for x in status['events'] if x['line'].startswith(f'BH phase={c}-repeat-{n} '))
        end=event['received_unix_ns']/1e9-created
        events.append({'step':n,'end_locator_s':end,'start_locator_s':end-p['total_us']/1e6})
    for name,t0 in markers.items():
        for i,offset in enumerate((0,.5,1)):
            t=t0+offset;assert 0<=t<probe['duration_s']
            target=out/f'{c}_{name}_{i}.png';assert not target.exists()
            cmd=[str(ffmpeg),'-hide_banner','-nostdin','-ss',f'{t:.6f}','-copyts','-i',str(source),'-map','0:v:0','-vf','format=gray,showinfo','-frames:v','1','-an','-update','1',str(target)]
            p=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            log=p.stderr.decode('utf-8','replace');assert p.returncode==0,log[-1500:]
            pts=re.findall(r'\bn:\s*\d+.*?pts_time:([\d.]+)',log);assert pts
            samples.append({'label':name,'offset_s':offset,'requested_s':t,'decoded_pts_s':float(pts[0]),'file':str(target)})
            if i==0:
                image=Image.open(target).convert('RGB');image.thumbnail((1280,1280));image.save(out/f'{c}_{name}_preview.png')
    modified=dt.datetime.fromisoformat(source_info['source_modified_utc'].replace('Z','+00:00')).timestamp()
    entry={**source_info,'probe':probe,'marker_locator_s':markers,'events':events,'samples':samples,
           'metadata_elapsed_s':modified-created,'duration_minus_metadata_elapsed_s':probe['duration_s']-(modified-created),
           'locator_method':'USB receipt wallclock minus PC original-file creation time; approximate, no calibrated camera synchronization; visually verify stable images/events'}
    entries.append(entry)
    print(json.dumps({k:v for k,v in entry.items() if k not in ('samples','probe')},indent=2),flush=True)
(out/'extraction.json').write_text(json.dumps(entries,indent=2),encoding='utf-8')
