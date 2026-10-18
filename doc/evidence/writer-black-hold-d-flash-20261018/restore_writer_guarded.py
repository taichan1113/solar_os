"""Application-only rollback, preserving CURRENT documents/bonds/settings.
First obtain a fresh pre-restore-usb.log with device_query.py, after closing
Writer/Sketch and confirming exclusive device ownership. Not run at deployment.
"""
from pathlib import Path
import argparse,hashlib,json,os,re
from esptool.cmds import detect_chip,attach_flash,read_flash,write_flash,reset_chip

parser=argparse.ArgumentParser()
parser.add_argument('--saved-and-exclusive',action='store_true',required=True)
args=parser.parse_args()
base=Path(__file__).resolve().parent;archive=base/'archive/20261018-writer-d-flash'
sha=lambda b:hashlib.sha256(b).hexdigest()
audit=json.loads((archive/'backup-audit.json').read_text())
deployment=json.loads((archive/'flash-summary.json').read_text())
assert deployment['result']=='PASS'
rollback=(archive/'application-rollback.bin').read_bytes()
assert sha(rollback)==audit['rollback_sha256']=='278665c184653f06a403bd56713e7bf5073131bead0420b3fd2191fa54b12ef6'
full=(archive/'full-flash-before.bin').read_bytes();assert sha(full)==audit['full_backup_sha256']
candidate=(base/'artifacts/firmware-app-only.bin').read_bytes()
assert sha(candidate)==deployment['artifact_sha256']
pre=(archive/'pre-restore-usb.log').read_text(encoding='utf-8')
sessions=pre.split('sessions\n',1)[1].split('ota status',1)[0]
assert not re.search(r'\bwriter\b|\bsketch\b',sessions,re.I),'Close and save documents before rollback'
for label in ('Running','Boot'):
 m=re.search(label+r':\s+(ota_[01]) \(slot ([01])\) addr (0x[0-9a-fA-F]+)',pre)
 assert m and m[1]==audit['active_slot'] and int(m[3],16)==int(audit['app_offset'],16)
assert not (archive/'restore-summary.json').exists(),'Restore has already been attempted: review results first'
regions=audit['protected_region_ranges'];address=int(audit['app_offset'],16)
lock=base.parent/'papermono-COM39-flash.lock'
fd=os.open(lock,os.O_CREAT|os.O_EXCL|os.O_WRONLY)
os.write(fd,f'pid={os.getpid()} writer_d_restore={archive}\n'.encode())
report=dict(result='RUNNING',port=audit['port'],active_slot=audit['active_slot'],app_offset=hex(address),
            rollback_sha256=sha(rollback),write_started=False,protected_regions={})
esp=None
try:
 esp=detect_chip(audit['port'],115200)
 assert esp.CHIP_NAME=='ESP32-S3'
 assert ':'.join(f'{v:02x}' for v in esp.read_mac())==audit['mac']
 esp=esp.run_stub();esp.change_baud(460800);attach_flash(esp)
 assert 1<<((esp.flash_id()>>16)&0xff)==0x1000000
 current={}
 for name,(offset,size) in regions.items():
  current[name]=read_flash(esp,offset,size,no_progress=True)
 assert current['partition_table']==full[0x8000:0x9000]
 ota_offset,ota_size=regions['otadata']
 assert current['otadata']==full[ota_offset:ota_offset+ota_size],'OTA selection changed; re-evaluate before writing'
 assert read_flash(esp,address,len(candidate),no_progress=True)==candidate
 report['write_started']=True
 write_flash(esp,[(address,rollback)],flash_size='keep',compress=True,no_progress=True)
 actual=read_flash(esp,address,len(rollback),no_progress=True)
 report['readback_sha256']=sha(actual);assert actual==rollback
 for name,(offset,size) in regions.items():
  value=read_flash(esp,offset,size,no_progress=True)
  report['protected_regions'][name]=dict(unchanged=value==current[name],sha256=sha(value))
  assert value==current[name],name
 report['result']='PASS';reset_chip(esp,'hard-reset')
except BaseException as error:
 report.update(result='FAILED',error=repr(error));raise
finally:
 (archive/'restore-summary.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
 if esp is not None:esp._port.close()
 os.close(fd);lock.unlink()
 print(json.dumps(report,indent=2),flush=True)
