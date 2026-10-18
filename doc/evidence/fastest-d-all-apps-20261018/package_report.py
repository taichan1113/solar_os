from pathlib import Path
import hashlib,json,shutil,subprocess
base=Path(__file__).resolve().parent;wt=base/'worktree'
a=Path(json.loads((base/'active-flash-archive.json').read_text(encoding='utf-8'))['archive'])
load=lambda p:json.loads(p.read_text(encoding='utf-8'))
r=load(a/'deployment-audit.json');assert r['result']=='D_EXCEPT_SKETCH_READBACK_PROTECTION_AND_USB_PASS'
confirm=load(a/'user-confirmations.json')
def git(*args):return subprocess.check_output(['git','-C',str(wt),*args],encoding='utf-8').strip()
report=(base/'report-template-final.md').read_text(encoding='utf-8')
values={'visual':confirm['visual'],'shell':confirm['shell'],'apps':confirm['apps'],'runtime':confirm['runtime']}
for key,value in values.items():report=report.replace('{{'+key+'}}',value)
assert '{{' not in report
out=wt/'doc/m5paper-mono-fastest-d-all-apps-flash-20261018.md'
out.write_text(report,encoding='utf-8')
ev=wt/'doc/evidence/fastest-d-all-apps-20261018';ev.mkdir(parents=True,exist_ok=True)
(ev/'.gitattributes').write_text('* -text whitespace=cr-at-eol\n*.log -diff\n',encoding='utf-8')
for name in ('build.log','build-result.json','host-main.log','host-main-result.json','host-extra.log',
             'host-extra-result.json','worktrees-before.json','worktrees-after.json','run_host.py',
             'build_candidate.py','audit_candidate.py','audit_worktrees.py','verify_flash.py',
             'prepare_flash.py','package_report.py','report-template-final.md','sketch_exception.py'):
 shutil.copyfile(base/name,ev/name)
for name in ('manifest.json','build-selection.txt'):shutil.copyfile(base/'artifacts'/name,ev/name)
for name in ('preflight-usb.log','prewrite-usb.log','flash.log','boot.log','post-flash-usb.log',
             'backup-audit.json','flash-summary.json','deployment-audit.json','user-authorization.json',
             'user-confirmations.json','manual-shell-usb.log','runtime-inspect-usb.log',
             'manual-apps-usb.log','final-runtime-usb.log'):
 if (a/name).exists():shutil.copyfile(a/name,ev/name)
for name in ('flash_writer_guarded.py','restore_writer_guarded.py','device_query.py'):
 shutil.copyfile(base/name,ev/name)
(ev/'changed-files.txt').write_text(git('diff','--name-only','655d6135','57b2f8ff')+'\n',encoding='utf-8')
initial=ev/'initial-global';initial.mkdir(exist_ok=True)
for name in ('build.log','build-result.json','host-main.log','host-main-result.json','host-extra.log','host-extra-result.json','manifest.json'):
 src=base/'attempt-all-apps-initial'/name
 if src.exists():shutil.copyfile(src,initial/name)
first=Path(json.loads((base/'active-flash-archive-initial.json').read_text(encoding='utf-8'))['archive'])
for name in ('flash-summary.json','deployment-audit.json','user-regression.json','manual-shell-usb.log','runtime-inspect-usb.log','manual-apps-usb.log','flash.log'):
 if (first/name).exists():shutil.copyfile(first/name,initial/name)
index={p.relative_to(ev).as_posix():{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
       for p in sorted(ev.rglob('*')) if p.is_file() and p.name!='evidence-index.json'}
(ev/'evidence-index.json').write_text(json.dumps(index,indent=2),encoding='utf-8')
shutil.copyfile(out,a/'flash-report.md')
print(json.dumps({'report':str(out),'raw_evidence_files':len(index)},indent=2))
