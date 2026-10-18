from pathlib import Path
import hashlib,json,re,shutil
base=Path(__file__).resolve().parent;wt=base/'worktree'
m=json.loads((base/'artifacts/manifest.json').read_text(encoding='utf-8'))
assert m['result']=='DAD_HOST_BUILD_PASS_AWAITING_EXPLICIT_FLASH_APPROVAL'
assert not m['device_accessed'] and not m['flash_performed']
ev=wt/'doc/evidence/black-hold-dad-20261018';assert not ev.exists();ev.mkdir(parents=True)
files=['build.log','build-result.json','host-single-verified.log','host-single-result.json','host-regressions.log','host-regressions-result.json',
       'other-worktrees-before.json','other-worktrees-after.json','run_single_host.sh','run_host.py','build_candidate.py','audit_candidate.py','audit_worktrees.py','capture_dad.py','package_report.py']
for name in files:shutil.copyfile(base/name,ev/name)
for name in ('manifest.json','build-selection.txt'):shutil.copyfile(base/'artifacts'/name,ev/name)
(ev/'.gitattributes').write_text('* -text whitespace=cr-at-eol\n*.log -diff\n',encoding='utf-8',newline='\n')
plan=dict(state='HOST_BUILD_COMPLETE_STOPPED_AT_GATE_1',sequence=['D','A','D'],measurement_limit=3,D_limit=2,A_limit=1,
          normal_waveform='baseline',no_intermediate_OTP=True,operator_gate_each_measurement=True,goal_from_previous_end_ms=60000,
          first_goal_from_preparation_end=True,T0_after_preparation_ms=5000,stable_measurement_after_end_ms=2000,
          planned_start_formula='max(previous_end+60000000,operator_request+5000000) microseconds',diagnostic_cmd20_if_complete=10,
          current_task_COM_accessed=False,primary_optical_metrics=['dC3=C3-C2','C3-C1'],
          recovery_ratio='(C3-C2)/(C1-C2), reference only when denominator clearly exceeds optical fluctuation',
          future_flash='Fresh device/slot/partition identification; new full/app backup; app-only write/readback/protection; return to original Application without data rollback',
          physical_lut_readback=False,DAD_classification='not measured',
          brief_previously_tolerated_whitening_alone_is_failure=False,
          gates_remaining=['1 DAD flash authorization','2 DAD results reviewed, Writer implementation authorization','3 Writer host/build reviewed, separate flash authorization','4 Writer results reviewed, Integration adoption decision'],
          Writer_Trial_implementation_started=False)
(ev/'trial-plan.json').write_text(json.dumps(plan,indent=2),encoding='utf-8')
index={p.name:{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(ev.iterdir()) if p.is_file()}
(ev/'evidence-index.json').write_text(json.dumps(index,indent=2),encoding='utf-8')
s=(base/'report-template.md').read_text(encoding='utf-8')
for key,value in {'IMPLEMENTATION':m['implementation_commit'],'APP_SIZE':f"{m['application_bytes']:,}",'APP_SHA':m['sha256'],
                  'APP_REMAINING':f"{m['ota_remaining_bytes']:,}",'BUILD_SECONDS':f"{m['build_seconds']:.1f}"}.items():s=s.replace('@'+key+'@',value)
assert not re.search(r'@[A-Z_]+@',s)
report=wt/'doc/m5paper-mono-black-hold-dad-20261018.md';assert not report.exists()
report.write_text(s,encoding='utf-8',newline='\n')
for link in re.findall(r'\]\(([^)]+)\)',s):assert (report.parent/link).exists(),link
print('Report/evidence written:',report,'; files:',len(index)+1)
