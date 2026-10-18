from pathlib import Path
import hashlib,json,re
base=Path(__file__).resolve().parent;archive=base/'archive/20261018-writer-d-flash'
sha=lambda b:hashlib.sha256(b).hexdigest()
m=json.loads((base/'artifacts/manifest.json').read_text())
r=json.loads((archive/'flash-summary.json').read_text())
assert r['result']=='PASS' and r['write_started']
assert r['artifact_sha256']==r['readback_sha256']==m['sha256']
backup=(archive/'full-flash-before.bin').read_bytes()
assert len(backup)==0x1000000 and sha(backup)==r['full_backup_sha256']
rollback=(archive/'application-rollback.bin').read_bytes()
assert sha(rollback)==r['rollback_sha256']=='278665c184653f06a403bd56713e7bf5073131bead0420b3fd2191fa54b12ef6'
address=int(r['app_offset'],16)
assert rollback==backup[address:address+len(rollback)]
part=(archive/'application-partition-before.bin').read_bytes()
assert part==backup[address:address+r['app_partition_size']]
assert len(r['protected_regions'])==7 and all(v['unchanged'] for v in r['protected_regions'].values())
post=(archive/'post-flash-usb.log').read_text(encoding='utf-8',errors='replace')
assert 'SolarOS 4.16.1' in post and 'Flavor: writerdeck' in post
for text in (f"Running: {r['active_slot']}",f"Boot: {r['active_slot']}",'launcher     launcher active',
             'mode: refresh=fastest','display mode display0 fastest-path=tile',
             'writer-lut err=0 mode=A attempt=0/6 D0x20=0 success=0 error=0 failed=0 cap=0',
             'writer physical=0 A=0 D=0 OTP=0 noop=0',
             'async enabled=1 busy=0 pending=0 suspended=0 error=0'):
 assert text in post,text
before_startup=(archive/'preflight-usb.log').read_text().split('cat /.shell/startup\n',1)[1].split('user@sol:/',1)[0].strip()
after_startup=post.split('cat /.shell/startup\n',1)[1].split('user@sol:/',1)[0].strip()
assert before_startup==after_startup
boot=(archive/'boot.log').read_text(encoding='utf-8',errors='replace')
assert not re.search(r'Guru Meditation|panic|BUSY.*timeout|assert failed',post+'\n'+boot,re.I)
assert not (base.parent/'papermono-COM39-flash.lock').exists()
confirmation=json.loads((archive/'user-confirmations.json').read_text(encoding='utf-8'))
assert confirmation['flash_authorized'] and confirmation['post_flash_user_confirmation']=='表示正常'
summary={'result':'WRITER_D_FLASH_READBACK_PROTECTION_AND_USB_PASS','port':r['port'],'mac':r['mac'],
         'slot':r['active_slot'],'offset':r['app_offset'],'application_sha256':r['readback_sha256'],
         'application_bytes':m['application_bytes'],'full_backup_sha256':sha(backup),
         'rollback_sha256':sha(rollback),'protected_region_count':7,'protected_regions_unchanged':True,
         'protection_comparison_scope':'after Application write, before boot',
         'normal_boot_PHY_calibration_store_logged':'Saving new calibration data' in boot,
         'post_boot_full_NVS_comparison_performed':False,
         'startup_preserved':True,'post_boot_usb_shell':True,'launcher_started':True,
         'writer_trial_mode':'baseline','D_attempts':0,'D_physical_updates':0,
         'Writer_trial_started':False,'optical_comparison_performed':False,
         'post_flash_visual_status':'user confirmed normal','restore_helper':'restore_writer_guarded.py'}
(archive/'deployment-audit.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
print(json.dumps(summary,indent=2))
