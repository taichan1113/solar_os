"""Compare two completed RAM Writer journals; never open a device or infer optics."""
import argparse
import json
from pathlib import Path
import analyze_writer_quality as journal


def compare(normal,coalesced):
    for a,policy in ((normal,'normal'),(coalesced,'coalesced')):
        if not a.get('valid') or a.get('protocol')!='writer-burst' or a.get('policy')!=policy:
            raise ValueError('complete normal and coalesced burst journals required')
    if normal['black_masks']!=coalesced['black_masks']:
        raise ValueError('initial ROI/black masks differ; quality comparison invalid')
    inputs=lambda a:[r['ch'] for r in a['rows']['input']]
    if inputs(normal)!=inputs(coalesced):raise ValueError('input content differs')
    selected=lambda a:dict(inputs=a['input_count'],renders=a['render_count'],physical=a['physical_count'],
        pending_replaced=a['replaced'],response={k:v for k,v in a['response'].items() if k!='mapping'},
        driver_p50_us=a['driver_p50_us'],observer_p50_us=a['observer_p50_us'],
        final_snapshot='verified',quality_C=None,quality_R=None)
    return dict(normal=selected(normal),coalesced=selected(coalesced),
        physical_reduction=normal['physical_count']-coalesced['physical_count'],
        following_reduction=normal['first_following_updates']-coalesced['first_following_updates'],
        maximum_latency_delta_us=coalesced['response']['maximum_us']-normal['response']['maximum_us'],
        decision='CONDITIONAL: optical comparison and human responsiveness acceptance pending',
        quality_status='unmeasured; logical unchanged pixels do not prove optical contrast retention')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--normal',required=True,type=Path);p.add_argument('--coalesced',required=True,type=Path)
    p.add_argument('--output',required=True,type=Path);p.add_argument('--plot',type=Path)
    p.add_argument('--basis',required=True,choices=('host-boundary-model','hardware'))
    a=p.parse_args()
    normal=journal.analyze(a.normal.read_text(encoding='utf-8'))
    coalesced=journal.analyze(a.coalesced.read_text(encoding='utf-8'))
    result=compare(normal,coalesced);result['basis']=a.basis
    a.output.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    if a.plot:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
        fig,axes=plt.subplots(1,2,figsize=(10,4),constrained_layout=True)
        for b,label in ((normal,'normal'),(coalesced,'coalesced')):
            points=[r for r in b['response']['mapping'] if r['seq']>20]
            axes[0].plot([r['seq']-20 for r in points],[r['latency_us']/1000 for r in points],'.-',label=label)
        axes[0].set(xlabel='Following input sequence',ylabel='Sent to Driver completion (ms)')
        axes[0].legend();axes[0].grid(alpha=.2)
        axes[1].bar(['normal','coalesced'],[normal['physical_count'],coalesced['physical_count']],color=['#1768ac','#268447'])
        axes[1].set(ylabel='Physical updates including one formation',ylim=(0,10))
        fig.suptitle(a.basis+'; optical quality unmeasured')
        fig.savefig(a.plot,dpi=160);plt.close(fig)
    print(json.dumps(result,ensure_ascii=False,indent=2))


if __name__=='__main__':main()
