"""Extend the previous B/D fixed-ROI analysis to C0..C3 and three local events."""
from pathlib import Path
import json,re,subprocess,statistics
import numpy as np
from PIL import Image,ImageDraw

base=Path(__file__).resolve().parent;root=base.parents[1]
run=base/'archive/20261018-dad';out=run/'analysis'
ffmpeg=root/'.research/writer-video-quality/python-deps/imageio_ffmpeg/binaries/ffmpeg-linux-x86_64-v7.0.2'
configs=json.loads((out/'roi-config.json').read_text(encoding='utf-8-sig'))
entries=json.loads((out/'extraction.json').read_text())
def crop(a,r):
    x0,y0,x1,y1=r;return a[y0:y1,x0:x1]
def metrics(a,rois,mask):
    result={}
    for name,b,w in (('square',crop(a,rois['square_black']),crop(a,rois['square_white'])),
                     ('text',crop(a,rois['text_black_patch'])[mask],crop(a,rois['text_white']))):
        black=float(b.mean());white=float(w.mean());assert white>0
        result.update({name+'_black':black,name+'_white':white,name+'_contrast':(white-black)/white,
                       name+'_black_clip_fraction':float((b<=1).mean()),name+'_white_clip_fraction':float((w>=254).mean())})
    return result
def med(rows):
    assert rows
    return {key:float(np.median([r[key] for r in rows])) for key in rows[0] if key not in ('label','offset_s','pts_s')}

results=[];sheets=[]
for entry in entries:
    c=entry['condition'];rois=configs[c]
    ref=np.asarray(Image.open(out/f'{c}_T0_0.png').convert('L'))
    mask=crop(ref,rois['text_black_patch'])<.65*crop(ref,rois['text_white']).mean()
    assert mask.sum()>1000
    samples=[]
    for sample in entry['samples']:
        a=np.asarray(Image.open(sample['file']).convert('L'))
        samples.append({'label':sample['label'],'offset_s':sample['offset_s'],'pts_s':sample['decoded_pts_s'],**metrics(a,rois,mask)})
    groups={f'T{i}':med([s for s in samples if s['label']==f'T{i}']) for i in range(4)}
    left=min(r[0] for r in rois.values())//2*2;top=min(r[1] for r in rois.values())//2*2
    width=(max(r[2] for r in rois.values())-left+1)//2*2
    height=(max(r[3] for r in rois.values())-top+1)//2*2
    local={k:[r[0]-left,r[1]-top,r[2]-left,r[3]-top] for k,r in rois.items()}
    source='/mnt/'+entry['source'][0].lower()+entry['source'][2:].replace(chr(92),'/')
    updates=[]
    for event in entry['events']:
        step=event['step'];end=event['end_locator_s'];start=event['start_locator_s']-5.25
        stop=end+3.25;n=int(np.ceil((stop-start)*entry['probe']['nominal_fps']))+2
        command=[str(ffmpeg),'-hide_banner','-nostdin','-ss',f'{start:.6f}','-copyts','-i',source,'-map','0:v:0','-vf',f'crop={width}:{height}:{left}:{top},format=gray,showinfo','-frames:v',str(n),'-fps_mode','passthrough','-an','-f','rawvideo','pipe:1']
        process=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        log=process.stderr.decode('utf-8','replace');assert process.returncode==0,log[-1500:]
        assert len(process.stdout)%(width*height)==0
        frames=np.frombuffer(process.stdout,dtype=np.uint8).reshape((-1,height,width))
        pts=[float(x) for x in re.findall(r'\bn:\s*\d+.*?pts_time:([\d.]+)',log)][:len(frames)]
        assert len(pts)==len(frames) and all(a<b for a,b in zip(pts,pts[1:]))
        timeline=[{'pts_s':t,**metrics(a,local,mask)} for t,a in zip(pts,frames)]
        def rows(lo,hi):
            selected=[x for x in timeline if lo<=x['pts_s']<=hi];assert len(selected)>=5;return selected
        before=med(rows(end-.75,end-.5));after=med(rows(end+.2,end+.45))
        guarded=rows(event['start_locator_s']-5,end-.5)
        early=med(rows(event['start_locator_s']-5,event['start_locator_s']-4.5))
        late=med(rows(end-1,end-.5));targets={}
        for target in ('square','text'):
            key=target+'_contrast';white=target+'_white';values=np.array([r[key] for r in timeline])
            smooth=np.array([np.median(values[max(0,i-1):i+2]) for i in range(len(values))])
            near=[i for i,t in enumerate(pts) if abs(t-end)<=1];assert near
            idx=min(near,key=lambda i:values[i]);midx=min(near,key=lambda i:smooth[i])
            w=np.array([r[white] for r in guarded]);v=np.array([r[key] for r in guarded])
            targets[target]={'local_pre_C':before[key],'raw_min_C':float(values[idx]),'raw_min_pts_s':pts[idx],
                'median3_min_C':float(smooth[midx]),'median3_min_pts_s':pts[midx],
                'raw_min_minus_local_pre_pp':100*(float(values[idx])-before[key]),
                'median3_min_minus_local_pre_pp':100*(float(smooth[midx])-before[key]),
                'stable_C':groups[f'T{step}'][key],'stable_minus_raw_min_pp':100*(groups[f'T{step}'][key]-float(values[idx])),
                'near_step_pp':100*(after[key]-before[key]),'near_white_change_percent':100*(after[white]/before[white]-1),
                'guarded_pre_white_range_percent':100*(float(max(w)-min(w)))/float(np.median(w)),
                'guarded_pre_white_early_to_late_percent':100*(late[white]/early[white]-1),
                'guarded_pre_C_range_pp':100*float(max(v)-min(v)),
                'guarded_pre_C_early_to_late_pp':100*(late[key]-early[key]),
                'guarded_pre_interval_s':[event['start_locator_s']-5,end-.5]}
        intervals=np.diff(pts)
        update={**event,'local_pre_interval_s':[end-.75,end-.5],'local_post_interval_s':[end+.2,end+.45],
                'targets':targets,'native_PTS':{'frames':len(pts),'min_interval_s':float(min(intervals)),
                'max_interval_s':float(max(intervals)),'gaps_gt_50ms':int(sum(intervals>.05))},
                'max_clip_fraction':{k:max(r[k] for r in timeline) for k in timeline[0] if 'clip_fraction' in k},'timeline':timeline}
        updates.append(update)
        center=min(range(len(pts)),key=lambda i:abs(pts[i]-targets['square']['raw_min_pts_s']))
        selected=list(range(center-3,center+4));assert min(selected)>=0 and max(selected)<len(frames)
        sheet=Image.new('RGB',(2520,240),'white');draw=ImageDraw.Draw(sheet)
        for column,i in enumerate(selected):
            tile=Image.fromarray(frames[i]).resize((360,193));sheet.paste(tile,(column*360,43))
            draw.text((column*360+4,4),f'{c}{step} PTS {pts[i]:.6f}s',fill='black')
            draw.text((column*360+4,20),f'end locator {pts[i]-end:+.3f}s',fill='black')
        sheet.save(out/f'{c}-step{step}-event-contact.png');sheets.append(sheet)
        print(f'{c} update {step}: {len(pts)} native frames analyzed',flush=True)
    changes={}
    for target in ('square','text'):
        key=target+'_contrast';white=target+'_white'
        cs=[groups[f'T{i}'][key] for i in range(4)]
        changes[target]={'C':cs,'delta_C_pp':[100*(x-cs[0]) for x in cs],
                         'dC_pp':[None]+[100*(b-a) for a,b in zip(cs,cs[1:])],
                         'white_change_from_T0_percent':[100*(groups[f'T{i}'][white]/groups['T0'][white]-1) for i in range(4)]}
    result={'condition':c,'source':entry['source'],'rois':rois,'text_mask_pixels':int(mask.sum()),
            'mask_policy':'immutable per-video T0, threshold 0.65 adjacent white','samples':samples,'summary':groups,
            'changes':changes,'updates':updates,'synchronization':entry['locator_method'],
            'repeated_frames_are_independent_trials':False}
    (out/f'{c}-roi.json').write_text(json.dumps(result,indent=2),encoding='utf-8');results.append(result)
    image=Image.open(out/f'{c}_T0_0.png').convert('RGB');draw=ImageDraw.Draw(image)
    for label,r in rois.items():
        color='red' if 'black' in label else 'blue';draw.rectangle(r,outline=color,width=5);draw.text((r[0],r[1]-20),label,fill=color)
    image.thumbnail((1600,1600));image.save(out/f'{c}-roi-overlay.png')
comparison=results[0]['changes']
(out/'comparison.json').write_text(json.dumps(comparison,indent=2),encoding='utf-8')
combined=Image.new('RGB',(2520,240*len(sheets)),'white')
for i,sheet in enumerate(sheets):combined.paste(sheet,(0,240*i))
combined.save(out/'DAD-event-contact.png')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
fig,axes=plt.subplots(2,2,figsize=(11,7))
for r in results:
    c=r['condition']
    for target,ax in zip(('square','text'),axes[0]):
        ax.plot(range(4),np.array(r['changes'][target]['C'])*100,'o-',label=c)
        ax.set_title(target+' contrast');ax.set_ylabel('Relative contrast (%)');ax.set_xlabel('Update count (0, D, A, D)');ax.set_xticks(range(4));ax.grid(alpha=.25);ax.legend()
    axes[1,0].plot(range(4),r['changes']['square']['white_change_from_T0_percent'],'o-',label=c)
    axes[1,1].plot((1,2,3),[u['targets']['square']['raw_min_minus_local_pre_pp'] for u in r['updates']],'o-',label=c)
axes[1,0].set_title('Adjacent white change from T0');axes[1,0].set_ylabel('%');axes[1,0].set_xticks(range(4))
axes[1,1].set_title('Local pre to native raw minimum (square)');axes[1,1].set_ylabel('Percentage points');axes[1,1].set_xticks((1,2,3))
for ax in axes[1]:ax.grid(alpha=.25);ax.legend();ax.set_xlabel('Update count (0, D, A, D)')
fig.tight_layout();fig.savefig(out/'contrast-summary.png',dpi=160);plt.close(fig)
print(json.dumps(comparison,indent=2),flush=True)
