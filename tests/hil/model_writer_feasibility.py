"""Host-only replay and bounded what-if scheduler for the PaperMono Writer trial.

Replay checks membership/ordering against independent submit and Driver records.
The scheduler uses measured warm work times, but is not a validated firmware or
optical prediction. It never opens a serial port or mutates firmware data.
"""
from __future__ import annotations

import argparse
import json
import math
import statistics
from pathlib import Path


def following(data):
    if not data['valid'] or data['protocol'] != 'writer-burst':
        raise ValueError('valid completed writer-burst journal required')
    rows = data['rows']
    inputs, frames, physical = rows['input'][20:], rows['frame'][1:], rows['physical'][1:]
    if len(rows['input']) != 29 or len(inputs) != 9 or len(physical) < 1:
        raise ValueError('unexpected formation/burst cardinality')
    if [v['seq'] for v in inputs] != list(range(21, 30)):
        raise ValueError('missing or out-of-order CHAR')
    if [v['ch'] for v in inputs] != list(map(ord, 'bcdefghij')):
        raise ValueError('unexpected input content')
    if rows['summary'][0]['replaced'] != len(frames)-len(physical):
        raise ValueError('pending replacement count inconsistent')
    if frames[-1]['chars'] != 29 or physical[-1]['chars'] != 29:
        raise ValueError('final snapshot does not contain all input')
    for v in physical:
        if v['err'] or v['chars'] < 21 or v['chars'] > 29:
            raise ValueError('Driver failure or invalid snapshot')
        f=next((f for f in frames if f['gen']==v['gen']),None)
        if f is None or (f['chars'],f['hash'])!=(v['chars'],v['hash']):
            raise ValueError('Driver generation/version/hash mismatch')
    for i in inputs:
        if not any(v['chars']>=i['seq'] for v in physical):
            raise ValueError('input absent from physical snapshots')
    return inputs, frames, physical


def metrics(inputs, physical, renders, replaced):
    latencies=[]
    for item in inputs:
        included=next((p for p in physical if p.get('version',p['chars'])>=item['seq']),None)
        if included is None:
            raise ValueError('trailing input has no containing physical snapshot')
        latencies.append(round(included['end']-item['sent']))
    ordered=sorted(latencies)
    return dict(inputs=len(inputs),renders=renders,physical=len(physical),replaced=replaced,
        first_us=latencies[0],p50_us=ordered[len(ordered)//2],
        p95_us=ordered[math.ceil(len(ordered)*.95)-1],maximum_us=max(latencies),
        final_us=latencies[-1],max_no_update_us=max([latencies[0]]+[b['end']-a['end'] for a,b in zip(physical,physical[1:])]),
        physical_chars=[p['chars'] for p in physical],latency_us=latencies,
        final_snapshot=physical[-1].get('version',physical[-1]['chars'])==inputs[-1]['seq'])


def replay(data):
    inputs,frames,physical=following(data)
    # Independent event membership: each Driver start must consume the latest
    # available submit, with active work retaining its immutable snapshot.
    jobs=[dict(time=f['submit_end'],chars=f['chars'],gen=f['gen'],hash=f['hash']) for f in frames]
    jobs.sort(key=lambda j:j['time'])
    queue=[]
    active=None
    consumed=[]
    for p in physical:
        eligible=[j for j in jobs if j['time']<=p['start'] and j not in consumed]
        if not eligible:
            raise ValueError('Driver started without a submitted frame')
        expected=eligible[-1]
        if (expected['gen'],expected['chars'],expected['hash']) != (p['gen'],p['chars'],p['hash']):
            raise ValueError('active/latest-pending generation replay mismatch')
        consumed.extend(eligible)
        queue.append((p['gen'],p['chars'],p['start'],p['end']))
        if active is not None and p['start']<active['end']:
            raise ValueError('overlapping Driver intervals')
        active=p
    measured=metrics(inputs,physical,len(frames),len(frames)-len(physical))
    for key in ('first_us','p50_us','p95_us','maximum_us','final_us','max_no_update_us'):
        if measured[key]!=data['response'][key]:
            raise ValueError(f'journal response mismatch: {key}')
    return dict(**measured, generations=queue,
                pending_replacement_verified=len(frames)-len(physical)==data['replaced'],
                replay_error_us=0,
                caveat='Recorded submits and Driver durations are inputs; replay is not out-of-sample prediction')


def measured_parameters(normal,coalesced):
    traces=[normal,coalesced]
    warm=[(p['end']-p['start']) for a in traces for p in a['rows']['physical'][1:]]
    submit=[f['submit_end']-t['time'] for a in traces for f in a['rows']['frame'][1:]
            for t in a['rows']['tick'] if t['allowed'] and abs(f['doc_end']-t['time'])<1500]
    startup=[p['start']-f['submit_end'] for a in traces for p in a['rows']['physical'][1:]
             for f in a['rows']['frame'][1:] if f['gen']==p['gen'] and p['start']-f['submit_end']<5000]
    input_pipeline=[f['doc_start']-a['rows']['input'][f['chars']-1]['sent'] for a in traces
                    for f in a['rows']['frame'][1:] if f['chars']>20 and
                    0<=f['doc_start']-a['rows']['input'][f['chars']-1]['sent']<2000]
    return dict(warm_us=round(statistics.median(warm)),submit_us=round(statistics.median(submit)),
                startup_us=round(statistics.median(startup)),
                input_pipeline_us=round(statistics.median(input_pipeline)),
                warm_range_us=[min(warm),max(warm)],
                submit_range_us=[min(submit),max(submit)],
                startup_range_us=[min(startup),max(startup)])


def simulate(input_us, tick_us, *, period_us=0, idle_us=80000,
             pending_gate=False, warm_us=114000, submit_us=7100,
             startup_us=900,input_pipeline_us=200, operations=None):
    """Discrete Writer tick/active/latest-pending model; all time in microseconds."""
    if not input_us or sorted(input_us)!=list(input_us):
        raise ValueError('sorted input times required')
    if not tick_us or sorted(tick_us)!=list(tick_us):
        raise ValueError('sorted tick times required')
    origin=input_us[0]
    if operations is None:
        operations='bcdefghij'[:len(input_us)] if len(input_us)<=9 else 'x'*len(input_us)
    if len(operations)!=len(input_us):
        raise ValueError('operation count differs from input count')
    ins=[dict(seq=i+21,sent=t,ch=ord(operations[i])) for i,t in enumerate(input_us)]
    available=0; last_input=None; last_submit=None; leading=True
    active=None; pending=None; completed=[]; submitted=[]; replaced=0
    content='a'*20

    def advance(now):
        nonlocal active,pending
        while active is not None and active['end']<=now:
            completed.append(active)
            finish=active['end']
            if pending is not None:
                active=pending
                active['start']=finish+startup_us
                active['end']=active['start']+warm_us
                pending=None
            else:
                active=None

    for tick in tick_us:
        advance(tick)
        while available<len(ins) and ins[available]['sent']+input_pipeline_us<=tick:
            now=ins[available]['sent']+input_pipeline_us
            if last_input is None or now-last_input>=idle_us:
                leading=True
            last_input=now
            op=operations[available]
            content=content[:-1] if op=='\b' else content+op
            available+=1
        if not available:
            continue
        has_new=not submitted or available+20>submitted[-1]['version']
        allowed=(not period_us or last_submit is None or leading or
                 tick-last_submit>=period_us or tick-last_input>=idle_us)
        if pending_gate and pending is not None and not leading:
            allowed=False
        if not has_new or not allowed:
            continue
        submit=tick+submit_us
        advance(submit)
        job=dict(chars=len(content),version=available+20,content=content,
                 gen=len(submitted)+1,time=submit)
        submitted.append(job)
        last_submit=submit
        leading=False
        if active is None:
            active=dict(job,start=submit+startup_us,end=submit+startup_us+warm_us)
        else:
            if pending is not None: replaced+=1
            pending=job
        if len(submitted)>30:
            raise ValueError('unbounded render attempts')
    advance(tick_us[-1]+1000000)
    if pending is not None or active is not None:
        raise ValueError('unflushed executor')
    if not completed or completed[-1]['version']!=20+len(ins) or completed[-1]['content']!=content:
        raise ValueError('final snapshot omitted')
    result=metrics(ins,completed,len(submitted),replaced)
    result.update(input_origin_us=origin,period_us=period_us,idle_us=idle_us,
                  pending_gate=pending_gate,model='estimated work times / tick schedule; not optically calibrated')
    return result


def observed_ticks(data):
    start=data['rows']['input'][20]['sent']
    ticks=[t['time']-start for t in data['rows']['tick'] if t['time']>=start]
    while len(ticks)<30:
        ticks.append(ticks[-1]+40000)
    return ticks[:30]


def representative_scenarios(params):
    policies=[('A120',dict(period_us=120000,idle_us=80000)),
              ('B160fastTrailing',dict(period_us=160000,idle_us=40000)),
              ('CpendingGate',dict(period_us=0,idle_us=80000,pending_gate=True))]
    scenarios={
        'burst60':[60000*i for i in range(9)],
        'burst60_jitter':[0]+[60000*i+(3000 if i%2 else -3000) for i in range(1,9)],
        'single':[0],
        'spacing100':[100000*i for i in range(9)],
        'pause':[0,60000,120000,360000,420000],
    }
    work={key:params[key] for key in ('warm_us','submit_us','startup_us','input_pipeline_us')}
    rows=[]
    for scenario,input_times in scenarios.items():
        for phase in (800,10800,20800,30800):
            ticks=list(range(40000+phase,input_times[-1]+800000,40000))
            for duration in (params['warm_range_us'][0],params['warm_range_us'][1]):
                settings={**work,'warm_us':duration}
                base=simulate(input_times,ticks,**settings)
                for name,policy in policies:
                    candidate=simulate(input_times,ticks,**policy,**settings)
                    max_delta=candidate['maximum_us']-base['maximum_us']
                    final_delta=candidate['final_us']-base['final_us']
                    row=dict(scenario=scenario,phase_us=phase,warm_us=duration,policy=name,
                             baseline_physical=base['physical'],physical=candidate['physical'],
                             first_delta_us=candidate['first_us']-base['first_us'],
                             p50_delta_us=candidate['p50_us']-base['p50_us'],
                             p95_delta_us=candidate['p95_us']-base['p95_us'],
                             max_delta_us=max_delta,final_delta_us=final_delta,
                             max_no_update_delta_us=candidate['max_no_update_us']-base['max_no_update_us'],
                             final_snapshot=candidate['final_snapshot'],
                             go=(candidate['physical']<base['physical'] and
                                 candidate['first_us']-base['first_us']<=10000 and
                                 max_delta<=30000 and final_delta<=30000 and
                                 candidate['max_no_update_us']-base['max_no_update_us']<=30000 and
                                 candidate['final_snapshot']))
                    rows.append(row)
    summary=[]
    for name,_ in policies:
        for scenario in scenarios:
            group=[r for r in rows if r['policy']==name and r['scenario']==scenario]
            summary.append(dict(policy=name,scenario=scenario,cases=len(group),
                go_cases=sum(r['go'] for r in group),
                physical_range=[min(r['physical'] for r in group),max(r['physical'] for r in group)],
                max_delta_range_us=[min(r['max_delta_us'] for r in group),max(r['max_delta_us'] for r in group)],
                final_delta_range_us=[min(r['final_delta_us'] for r in group),max(r['final_delta_us'] for r in group)],
                max_no_update_delta_range_us=[min(r['max_no_update_delta_us'] for r in group),max(r['max_no_update_delta_us'] for r in group)]))
    return dict(rows=rows,summary=summary,
                caveat='Four representative Tick phases, two observed warm-time bounds; scenarios are not an exhaustive or calibrated probability distribution')


def evaluate(journals):
    normal,coalesced=journals
    replayed=[replay(a) for a in journals]
    params=measured_parameters(*journals)
    policies=[('normal',dict(period_us=0,idle_us=80000)),
              ('trial160',dict(period_us=160000,idle_us=80000)),
              ('A120',dict(period_us=120000,idle_us=80000)),
              ('B160fastTrailing',dict(period_us=160000,idle_us=40000)),
              ('CpendingGate',dict(period_us=0,idle_us=80000,pending_gate=True))]
    rows=[]
    for source in journals:
        input_us=[i['sent']-source['rows']['input'][20]['sent'] for i in source['rows']['input'][20:]]
        ticks=observed_ticks(source)
        for name,policy in policies:
            result=simulate(input_us,ticks,**policy,**{k:params[k] for k in ('warm_us','submit_us','startup_us','input_pipeline_us')})
            rows.append(dict(source=source['policy'],policy=name,**result))
    return dict(parameters=params,replay=dict(C0=replayed[0],C1=replayed[1]),simulations=rows,
                scenarios=representative_scenarios(params),
                caveat='C0/C1 measured replay is distinct from estimated scheduler predictions')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--c0',type=Path,required=True)
    parser.add_argument('--c1',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    result=evaluate([json.loads(path.read_text(encoding='utf-8')) for path in (args.c0,args.c1)])
    args.output.write_text(json.dumps(result,indent=2,ensure_ascii=False),encoding='utf-8')
    for row in result['simulations']:
        print(row['source'],row['policy'],row['physical'],row['replaced'],
              row['first_us']/1000,row['p50_us']/1000,row['maximum_us']/1000,row['final_us']/1000,
              row['physical_chars'])


if __name__=='__main__':
    main()
