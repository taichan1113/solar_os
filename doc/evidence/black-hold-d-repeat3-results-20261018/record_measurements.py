"""Reuse the single-shot log audit for three gated updates; no device IO."""
from pathlib import Path
import argparse,json,re,statistics

parser=argparse.ArgumentParser();parser.add_argument('condition',choices='AD');args=parser.parse_args()
base=Path(__file__).resolve().parent
run=base/'archive/20261018-d-repeat3';c=args.condition
status=json.loads((run/f'{c}-status.json').read_text(encoding='utf-8-sig'))
assert status['done'] and status['ended'] and not status['fault'] and status['completed']==3
assert status['boot_attempts']==3 and status['diagnostic_cmd20']==10
raw=(run/f'{c}-serial.log').read_text(encoding='utf-8',errors='replace')
phases=[];ref=None;notes=[]
def fields(line):
    return {k:int(v) for k,v in re.findall(r'\b(\w+)=(-?\d+)\b',line)}
for line in raw.splitlines():
    if 'epd_ssd1677: REF err=' in line:
        ref=fields(line)
    if line.startswith('BH phase='):
        # The preparation line exceeds the Shell's formatting buffer and can
        # join its trace. Preserve the raw log, use the complete REF RAM record.
        part=line.split('BH trace ',1)[0]
        values=fields(part);values['phase']=re.search(r'BH phase=(\S+)',part)[1]
        if values['phase']=='prepare-baseline' or '-repeat-' in values['phase']:
            assert ref is not None and ref['start']==values['start_us']
            if values.get('ram_bytes')!=ref['ram_bytes']:
                assert values['phase']=='prepare-baseline' and 'BH trace ' in line
                notes.append({'phase':values['phase'],'issue':'BH preparation RAM tail truncated; use complete REF line',
                              'raw_BH_ram_field':values.get('ram_bytes'),'REF_ram_bytes':ref['ram_bytes']})
            values['ram_bytes']=ref['ram_bytes'];values['ram_source']='complete REF line, checked against BH when available'
        else:
            values['ram_bytes']=None # OTP payload is not populated in this timing field.
        phases.append(values)
expected=['white-otp','image-otp','prepare-baseline']+[f'{c}-repeat-{i}' for i in (1,2,3)]+['exit-otp']
assert [p['phase'] for p in phases]==expected
assert sum(p['cmd20'] for p in phases)==10
assert all(p['err']==0 and p['spi_hz']==20000000 for p in phases)
markers={m[1]:fields(m[0]) for m in re.finditer(r'^BH mark=(\S+)[^\n]*',raw,re.M)}
traces={m[1]:fields(m[0]) for m in re.finditer(r'BH trace phase=(\S+)[^\n]*',raw)}
requests={fields(m[0])['step']:fields(m[0]) for m in re.finditer(r'^BH request [^\n]*',raw,re.M)}
measured=phases[3:6];previous=phases[2];intervals=[]
for n,p in enumerate(measured,1):
    assert p['candidate']==(3 if c=='A' else 2) and p['cmd20']==1 and p['cmd32']==2
    assert p['cmd03']==p['cmd04']==p['cmd2c']==1 and p['ram_bytes']==144000
    assert p['end_us']-p['start_us']==p['total_us'] and requests[n]['boot_attempts']==n
    t=traces[p['phase']]
    assert t['lut_ready_us']<=t['activate_call_us']<=t['busy_wait_end_us']<=t['restore_start_us']<=t['restore_end_us']<=p['end_us']
    assert t['busy_edge_measured']==0 and t['busy_wait_end_us']-t['activate_call_us']==p['busy_us']
    assert markers[f'T{n}']['actual_us']>=markers[f'T{n}']['target_us']
    intervals.append({'step':n,'operator_request_us':requests[n]['requested_us'],
                      'request_to_driver_start_us':p['start_us']-requests[n]['requested_us'],
                      'previous_measurement_start_to_start_us':None if n==1 else p['start_us']-previous['start_us'],
                      'previous_completion_to_driver_start_us':p['start_us']-previous['end_us'],
                      'measurement_end_to_Tn_us':markers[f'T{n}']['actual_us']-p['end_us']})
    previous=p
result={'condition':c,'phases':phases,'measured':measured,'markers':markers,'traces':traces,
        'requests':requests,'actual_intervals_us':intervals,'prepare_to_T0_us':markers['T0']['actual_us']-phases[2]['end_us'],
        'diagnostic_cmd20':10,'measured_cmd20':3,'cmd32_measurement_total':6,
        'means_us':{key:statistics.mean(p[key] for p in measured) for key in ('total_us','busy_us','spi_us')},
        'log_quality_notes':notes,'optical_contrast':'not yet analyzed','physical_lut_readback':False,
        'total_scope':'diagnostic only; normal Shell/Launcher/boot updates excluded'}
target=run/f'{c}-measurements.json';assert not target.exists(),'Preserve previous audit'
target.write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps({k:result[k] for k in ('condition','diagnostic_cmd20','measured_cmd20','means_us','actual_intervals_us','log_quality_notes')},indent=2))
