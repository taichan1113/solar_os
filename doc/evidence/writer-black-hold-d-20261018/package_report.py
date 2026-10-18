from pathlib import Path
import hashlib,json,re,shutil,subprocess
base=Path(__file__).resolve().parent;wt=base/'worktree'
m=json.loads((base/'artifacts/manifest.json').read_text())
assert m['result']=='WRITER_D_HOST_BUILD_PASS_AWAITING_SEPARATE_FLASH_APPROVAL'
assert not m['device_accessed'] and not m['flash_performed']
root=wt/'doc/evidence/writer-black-hold-d-20261018';root.mkdir(parents=True,exist_ok=True)
files=['build.log','build-result.json','initial-failed-build.log','initial-failed-build-result.json',
       'host-main.log','host-main-result.json','host-extra.log','host-extra-result.json',
       'other-worktrees-before.json','other-worktrees-after.json','run_host.py','build_candidate.py',
       'audit_candidate.py','audit_worktrees.py','package_report.py']
for name in files:shutil.copyfile(base/name,root/name)
for name in ('manifest.json','build-selection.txt'):shutil.copyfile(base/'artifacts'/name,root/name)
(root/'.gitattributes').write_text('* -text whitespace=cr-at-eol\n*.log -diff\n',encoding='utf-8',newline='\n')
changed=subprocess.check_output(['git','diff',m['base_commit'],m['implementation_commit'],'--name-only'],cwd=wt,text=True)
(root/'changed-files.txt').write_text(changed,encoding='utf-8',newline='\n')
index={p.name:{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
       for p in sorted(root.iterdir()) if p.is_file() and p.name!='evidence-index.json'}
(root/'evidence-index.json').write_text(json.dumps(index,indent=2),encoding='utf-8')
s=(base/'report-template.md').read_text(encoding='utf-8')
for key,value in {'APP_SHA':m['sha256'],'APP_SIZE':f"{m['application_bytes']:,}",
                  'APP_REMAINING':f"{m['ota_remaining_bytes']:,}",
                  'BUILD_SECONDS':f"{m['build_seconds']:.1f}",
                  'SOURCE_COMMIT':m['implementation_commit']}.items():s=s.replace('@'+key+'@',value)
assert not re.search(r'@[A-Z_]+@',s)
report=wt/'doc/m5paper-mono-writer-black-hold-d-20261018.md'
report.write_text(s,encoding='utf-8',newline='\n')
for target in re.findall(r'\]\(([^)]+)\)',s):
 if target.startswith('https://'):continue
 assert (report.parent/target.split('#')[0]).is_file(),target
print(report,'evidence files',len(index)+1)
