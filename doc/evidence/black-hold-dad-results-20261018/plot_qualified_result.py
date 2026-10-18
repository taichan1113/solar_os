from pathlib import Path
import json,numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
out=Path(__file__).resolve().parent/'archive/20261018-dad/analysis'
r=json.loads((out/'DAD-roi.json').read_text())
fig,axes=plt.subplots(2,2,figsize=(11,7))
for name,ax in zip(('text','square'),axes[0]):
 ax.plot(range(4),np.array(r['changes'][name]['C'])*100,'o-')
 ax.set_title('Text: primary, no clipping' if name=='text' else 'Square: clipped after D, reference only')
 ax.set_ylabel('Relative video contrast (%)')
for name in ('text','square'):
 axes[1,0].plot(range(4),r['changes'][name]['white_change_from_T0_percent'],'o-',label=name)
 axes[1,1].plot(range(4),[100*r['summary'][f'T{i}'][name+'_black_clip_fraction'] for i in range(4)],'o-',label=name)
axes[1,0].set_title('Adjacent white change from T0');axes[1,0].set_ylabel('%')
axes[1,1].set_title('Black pixels at video floor (<=1 / 255)');axes[1,1].set_ylabel('ROI pixels (%)')
for ax in axes.flat:ax.set_xticks(range(4),('C0','C1: D','C2: A','C3: D'));ax.grid(alpha=.25)
for ax in axes[1]:ax.legend()
fig.suptitle('D-A-D: one short session; uncalibrated 8-bit video')
fig.tight_layout();fig.savefig(out/'contrast-summary-qualified.png',dpi=160);plt.close(fig)
