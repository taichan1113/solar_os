from pathlib import Path
import hashlib,json,re,shutil,subprocess
base=Path(__file__).resolve().parent;wt=base/'worktree';archive=base/'archive/20261018-writer-d-flash'
audit=json.loads((archive/'deployment-audit.json').read_text())
assert audit['result']=='WRITER_D_FLASH_READBACK_PROTECTION_AND_USB_PASS'
assert audit['post_flash_visual_status']=='user confirmed normal' and audit['D_attempts']==0
evidence=wt/'doc/evidence/writer-black-hold-d-flash-20261018'
evidence.mkdir(parents=True,exist_ok=True)
for name in ('usb-ports.json','preflight-usb.log','backup-audit.json','flash.log','boot.log',
             'flash-summary.json','post-flash-usb.log','deployment-audit.json','user-confirmations.json'):
 shutil.copyfile(archive/name,evidence/name)
for name in ('device_query.py','flash_writer_guarded.py','restore_writer_guarded.py',
             'verify_deployment.py','package_flash_report.py'):
 shutil.copyfile(base/name,evidence/name)
shutil.copyfile(base/'other-worktrees-after.json',evidence/'other-worktrees-after.json')
(evidence/'.gitattributes').write_text('* -text whitespace=cr-at-eol\n*.log -diff\n',encoding='utf-8',newline='\n')
index={p.name:{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
       for p in sorted(evidence.iterdir()) if p.is_file() and p.name!='evidence-index.json'}
(evidence/'evidence-index.json').write_text(json.dumps(index,indent=2),encoding='utf-8')
s=(base/'flash-report-template.md').read_text(encoding='utf-8')
s=s.replace('@VISUAL@','ユーザー確認：表示正常').replace('@BACKUP_SHA@',audit['full_backup_sha256'])
assert not re.search(r'@[A-Z_]+@',s)
report=wt/'doc/m5paper-mono-writer-black-hold-d-flash-20261018.md'
report.write_text(s,encoding='utf-8',newline='\n')
for name in re.findall(r'\]\(([^)]+)\)',s):assert (report.parent/name).is_file(),name
assert not subprocess.check_output(['git','diff','251232824da2f5350dbdf9184e0398b434a61497','HEAD','--','src','scripts','tests'],cwd=wt)
print(report,'evidence files',len(index)+1)
