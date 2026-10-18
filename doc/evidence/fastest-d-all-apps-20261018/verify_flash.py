from pathlib import Path
import hashlib,json,re
base=Path(__file__).resolve().parent
a=Path(json.loads((base/'active-flash-archive.json').read_text(encoding='utf-8'))['archive'])
load=lambda p:json.loads(p.read_text(encoding='utf-8'))
sha=lambda data:hashlib.sha256(data).hexdigest()
m=load(base/'artifacts/manifest.json');r=load(a/'flash-summary.json')
assert r['result']=='PASS' and r['write_started']
assert r['artifact_sha256']==r['readback_sha256']==m['sha256']
backup=(a/'full-flash-before.bin').read_bytes()
assert len(backup)==0x1000000 and sha(backup)==r['full_backup_sha256']
rollback=(a/'application-rollback.bin').read_bytes()
assert sha(rollback)==r['rollback_sha256']=='769dc0f5d835f70009ea054a021ae1bb5546d7a909a069c1dfd7f53f204375e4'
address=int(r['app_offset'],16)
assert rollback==backup[address:address+len(rollback)]
assert (a/'application-partition-before.bin').read_bytes()==backup[address:address+r['app_partition_size']]
assert len(r['protected_regions'])==7 and all(v['unchanged'] for v in r['protected_regions'].values())
post=(a/'post-flash-usb.log').read_text(encoding='utf-8',errors='replace')
pre=(a/'preflight-usb.log').read_text(encoding='utf-8',errors='replace')
boot=(a/'boot.log').read_text(encoding='utf-8',errors='replace')
for expected in ('SolarOS 4.16.1','Flavor: writerdeck',f"Running: {r['active_slot']}",
                 f"Boot: {r['active_slot']}",'mode: refresh=fastest',
                 'fastest-lut err=0 mode=D scope=all-apps',
                 'async enabled=1 busy=0 pending=0 suspended=0 error=0'):
 assert expected in post,expected
status=re.search(r'fastest-lut err=0 mode=D scope=all-apps-except-sketch attempts=(\d+) D0x20=(\d+) success=(\d+) error=(\d+) driver_error=(\d+) failed=(\d+)',post)
assert status
attempts,physical,success,errors,driver_errors,failed=map(int,status.groups())
assert attempts==physical==success and errors==driver_errors==failed==0
assert not re.search(r'Guru Meditation|panic|BUSY.*timeout|assert failed',post+'\n'+boot,re.I)
def startup(log):return log.split('cat /.shell/startup\n',1)[1].split('user@sol:/',1)[0].strip()
assert startup(pre)==startup(post)
assert not (base.parent/'papermono-COM39-flash.lock').exists()
result={'result':'D_EXCEPT_SKETCH_READBACK_PROTECTION_AND_USB_PASS',
 'source_commit':m['implementation_commit'],'integration_branch':'integration/papermono-dirty-sketch',
 'port':r['port'],'mac':r['mac'],'slot':r['active_slot'],'offset':r['app_offset'],
 'application_bytes':m['application_bytes'],'application_sha256':r['readback_sha256'],
 'full_backup_sha256':sha(backup),'rollback_sha256':sha(rollback),'rollback_bytes':len(rollback),
 'protected_regions_unchanged':True,'protected_region_count':7,
 'protection_comparison_scope':'after Application write, before reboot',
 'post_boot_full_NVS_comparison_performed':False,
 'normal_boot_PHY_calibration_store_logged':'Saving new calibration data' in boot,
 'startup_preserved':True,'post_boot_usb_shell':True,
 'launcher_running':bool(re.search(r'launcher\s+launcher\s+active',post)),
 'default_mode':'D','scope':'all-apps-except-sketch','Sketch_policy':'Baseline A','refresh_scope':'fastest','cumulative_D_limit':None,
 'post_boot_D_physical_updates':physical,'post_boot_D_success':success,'post_boot_errors':0,
 'automatic_input_performed':False,'boot_SPI_20MHz_logged':'actual_khz=20000' in boot+post,
 'new_firmware_all_app_optical_quality_verified':False,'visual_confirmation':'pending user',
 'safe_rollback_sha256':'6e3c6a02b701bf5b5b41a02c36e767ce7dbd4efdda338223c1f7f93f689551a1','rollback_helper':str(base/'restore_writer_guarded.py')}
(a/'deployment-audit.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(result,indent=2))
