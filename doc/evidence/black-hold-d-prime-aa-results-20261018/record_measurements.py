"""Audit split BH logs for the single authorized D/A/A session. No device IO."""
from pathlib import Path
import json, re, statistics

base = Path(__file__).resolve().parent
run = base / 'archive/20261018-d-prime-aa'
status = json.loads((run/'daa-status.json').read_text(encoding='utf-8'))
assert status['done'] and status['ended'] and not status['fault']
assert status['completed'] == status['boot_attempts'] == 3
assert status['diagnostic_cmd20'] == 10
raw = (run/'daa-usb.log').read_text(encoding='utf-8', errors='replace')
def fields(line):
    return {k:int(v) for k,v in re.findall(r'\b(\w+)=(-?\d+)\b', line)}
phases = []
for line in raw.splitlines():
    if line.startswith('BH phase='):
        phases.append(dict(fields(line), phase=re.search(r'BH phase=(\S+)', line)[1]))
    elif line.startswith('BH timing phase='):
        name = re.search(r'phase=(\S+)', line)[1]
        assert phases[-1]['phase'] == name
        phases[-1].update(fields(line))
expected = ['white-otp','image-otp','prepare-baseline'] + [f'DAA-repeat-{i}' for i in (1,2,3)] + ['exit-otp']
assert [p['phase'] for p in phases] == expected
assert sum(p['cmd20'] for p in phases) == 10
assert all(p['err'] == 0 and p['spi_hz'] == 20000000 for p in phases)
def named(prefix, name):
    return {m[1]:fields(m[0]) for m in re.finditer(r'^BH '+prefix+' '+name+r'=(\S+)[^\n]*',raw,re.M)}
markers = {m[1]:fields(m[0]) for m in re.finditer(r'^BH mark=(\S+)[^\n]*',raw,re.M)}
traces = named('trace', 'phase')
lut_lines = [line for line in raw.splitlines() if line.startswith('BH lut phase=')]
luts = {re.search(r'phase=(\S+)',line)[1]: dict(fields(line), requested_lut0=re.search(r'requested_lut0=(\S+)',line)[1]) for line in lut_lines}
requests = {fields(m[0])['step']:fields(m[0]) for m in re.finditer(r'^BH request [^\n]*',raw,re.M)}
schedules = {fields(m[0])['step']:fields(m[0]) for m in re.finditer(r'^BH schedule [^\n]*',raw,re.M)}
interval_lines = {fields(m[0])['step']:fields(m[0]) for m in re.finditer(r'^BH interval [^\n]*',raw,re.M)}
measured = phases[3:6]
previous = phases[2]
intervals = []
for n,p in enumerate(measured,1):
    assert p['candidate'] == (2 if n == 1 else 3)
    assert p['cmd20'] == 1 and p['cmd32'] == 2
    assert p['cmd03'] == p['cmd04'] == p['cmd2c'] == 1
    assert p['ram_bytes'] == 144000 and p['end_us'] - p['start_us'] == p['total_us']
    assert requests[n]['boot_attempts'] == n
    lut = luts[p['phase']]
    assert lut['requested_lut0'] == ('2FF0' if n == 1 else '0000')
    assert lut['lut_send_ok'] == lut['baseline_restore_ok'] == 1 and lut['physical_readback'] == 0
    t = traces[p['phase']]
    assert t['lut_ready_us'] <= t['activate_call_us'] <= t['busy_wait_end_us'] <= t['restore_start_us'] <= t['restore_end_us'] <= p['end_us']
    assert t['busy_edge_measured'] == 0
    assert t['busy_wait_end_us'] - t['activate_call_us'] == p['busy_us']
    sch = schedules[n]
    assert sch['previous_end_us'] == previous['end_us']
    assert sch['goal_us'] == previous['end_us'] + 60000000
    assert sch['planned_us'] == max(sch['goal_us'], sch['requested_us'] + 5000000)
    assert p['start_us'] >= sch['planned_us']
    gap = p['start_us'] - previous['end_us']
    assert gap == interval_lines[n]['end_to_start_us']
    assert markers[f'T{n}']['actual_us'] >= p['end_us'] + 2000000
    intervals.append(dict(step=n, waveform='D' if n==1 else 'A', previous_completion_to_driver_start_us=gap,
        target_lateness_us=interval_lines[n]['lateness_us'], measurement_end_to_Tn_us=markers[f'T{n}']['actual_us']-p['end_us']))
    previous=p
assert len(requests) == len(measured) == len(schedules) == 3
assert (run/'daa-control.txt').read_bytes() == b'ggggg'
result = dict(condition='DAA', sequence=['D','A','A'], phases=phases, measured=measured, markers=markers,
    traces=traces,luts=luts,requests=requests,schedules=schedules,actual_intervals_us=intervals,
    diagnostic_cmd20=10, measured_cmd20=3, D_cmd20=1, A_cmd20=2,
    means_us={key:statistics.mean(p[key] for p in measured) for key in ('total_us','busy_us','spi_us')},
    physical_lut_readback=False, intermediate_OTP_or_image_reinitialization=False,
    visual='T0 normal; T1,T2,T3 nearly unchanged; white normal; no conspicuous flicker',
    scope='Diagnostic only; normal UI and reboot updates excluded; optical video pending')
target=run/'DAA-measurements.json'
assert not target.exists()
target.write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps({k:result[k] for k in ('sequence','diagnostic_cmd20','means_us','actual_intervals_us')},indent=2))
