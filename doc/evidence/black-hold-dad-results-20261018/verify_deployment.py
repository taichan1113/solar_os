from pathlib import Path
import hashlib,json,re

base=Path(__file__).resolve().parent
archive=base/'archive/20261018-dad'
load=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
sha=lambda b:hashlib.sha256(b).hexdigest()
manifest=load(base/'artifacts/manifest.json')
report=load(archive/'flash-summary.json')
assert report['result']=='PASS' and report['write_started']
assert report['artifact_sha256']==report['readback_sha256']==manifest['sha256']
assert report['artifact_size']==manifest['application_bytes']
backup=(archive/'full-flash-before.bin').read_bytes()
assert len(backup)==0x1000000 and sha(backup)==report['full_backup_sha256']
rollback=(archive/'ota0-app-rollback.bin').read_bytes()
assert sha(rollback)==report['rollback_sha256']=='278665c184653f06a403bd56713e7bf5073131bead0420b3fd2191fa54b12ef6'
assert rollback==backup[0x10000:0x10000+len(rollback)]
assert len(report['protected_regions'])==6 and all(x['unchanged'] for x in report['protected_regions'].values())
post=(archive/'post-flash-usb.log').read_text(encoding='utf-8',errors='replace')
assert 'SolarOS 4.16.1' in post and 'Flavor: writerdeck' in post
for value in ('Running: ota_0','Boot: ota_0','launcher     launcher active','refresh=fastest','fastest-path=tile'):
    assert value in post,value
assert not re.search(r'Guru Meditation|panic|BUSY.*timeout|BH phase=',post,re.I)
assert not (base.parent/'papermono-COM39-flash.lock').exists()
summary={'result':'FLASH_READBACK_AND_PROTECTION_PASS','port':'COM39','mac':report['mac'],
         'slot':'ota_0','offset':'0x10000','application_sha256':report['readback_sha256'],
         'full_backup_sha256':report['full_backup_sha256'],'rollback_sha256':report['rollback_sha256'],
         'protected_regions_unchanged':True,'post_boot_usb_shell':True,'launcher_started':True,
         'startup_fastest_tile_preserved':True,'measured_A_updates':0,'measured_D_updates':0,'fixed_sequence':['D','A','D'],
         'post_flash_visual_status':'awaiting user reply','trial_started':False,
         'current_firmware':'DAD trial; normal waveform remains Baseline; do not reboot to renew trial budget',
         'restore_helper':'restore_single_guarded.py; run after trial with current slot checked; application-only'}
(archive/'deployment-audit.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
print(json.dumps(summary,indent=2))
