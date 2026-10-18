"""Small offline sensitivity/PTS check; never choose ROIs by effect size."""
from pathlib import Path
import json
import numpy as np
from PIL import Image

base=Path(__file__).resolve().parent;out=base/'archive/20261018-d-prime-aa/analysis'
configs=json.loads((out/'roi-config.json').read_text(encoding='utf-8-sig'))
entries=json.loads((out/'extraction.json').read_text())
def crop(a,r):
    x,y,X,Y=r;return a[y:Y,x:X]
result={}
for entry in entries:
    c=entry['condition'];rois=configs[c];ref=np.asarray(Image.open(out/f'{c}_T0_0.png').convert('L'))
    masks={str(t):crop(ref,rois['text_black_patch'])<t*crop(ref,rois['text_white']).mean() for t in (.55,.65,.75)}
    values={key:{f'T{i}':[] for i in range(4)} for key in masks}
    squares={f'T{i}':[] for i in range(4)}
    for sample in entry['samples']:
        a=np.asarray(Image.open(sample['file']).convert('L'));white=float(crop(a,rois['text_white']).mean())
        for key,mask in masks.items():
            black=float(crop(a,rois['text_black_patch'])[mask].mean());values[key][sample['label']].append((white-black)/white)
        x,y,X,Y=rois['square_black'];black=float(crop(a,[x+20,y+20,X-20,Y-20]).mean());white=float(crop(a,rois['square_white']).mean())
        squares[sample['label']].append((white-black)/white)
    changes={}
    for key,stages in {**values,'square_inset20':squares}.items():
        cs=[float(np.median(stages[f'T{i}'])) for i in range(4)]
        changes[key]={'C':cs,'delta_C3_pp':100*(cs[3]-cs[0]),'delta_C_pp':[100*(v-cs[0]) for v in cs], 'dC_pp':[None]+[100*(b-a) for a,b in zip(cs,cs[1:])]}
    roi=json.loads((out/f'{c}-roi.json').read_text());gaps=[]
    for update in roi['updates']:
        for a,b in zip(update['timeline'],update['timeline'][1:]):
            if b['pts_s']-a['pts_s']>.05:
                gaps.append({'step':update['step'],'gap_s':b['pts_s']-a['pts_s'],
                             'relative_to_end_s':[a['pts_s']-update['end_locator_s'],b['pts_s']-update['end_locator_s']]})
    result[c]={'sensitivity':changes,'text_mask_sizes':{k:int(m.sum()) for k,m in masks.items()},'native_PTS_gaps':gaps,
               'exposure_duplication_inside_camera':'unverified even if PTS intervals are regular'}
assert len(result)==1 and 'DAA' in result
(out/'robustness.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result,indent=2))
