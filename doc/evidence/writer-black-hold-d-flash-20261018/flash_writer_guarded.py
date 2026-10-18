"""User-authorized, fresh-backup, application-only Writer D deployment.
No mode selection, optical test, startup modification or data erase.
"""
from pathlib import Path
import hashlib,json,os,re,struct,zlib,subprocess,time
from esptool.cmds import detect_chip,attach_flash,read_flash,write_flash,reset_chip

base=Path(__file__).resolve().parent
archive=base/'archive/20261018-writer-d-flash'
sha=lambda b:hashlib.sha256(b).hexdigest()
manifest=json.loads((base/'artifacts/manifest.json').read_text())
candidate=(base/'artifacts/firmware-app-only.bin').read_bytes()
assert sha(candidate)==manifest['sha256']=='517250fa47fd1d702fccf28e5f3da6d4557fca978bd6f9e953d589ed6b29a253'
assert len(candidate)==manifest['application_bytes']==3458400
assert not subprocess.check_output(['git','diff',manifest['implementation_commit'],'HEAD','--','src','scripts','tests'],cwd=base/'worktree')
known=(base.parent/'dirty-sketch-integration/artifacts-typemouse/firmware-app-only.bin').read_bytes()
assert sha(known)=='278665c184653f06a403bd56713e7bf5073131bead0420b3fd2191fa54b12ef6'
pre=(archive/'preflight-usb.log').read_text(encoding='utf-8')
def ota_line(label):
 m=re.search(label+r':\s+(ota_[01]) \(slot ([01])\) addr (0x[0-9a-fA-F]+)',pre)
 assert m,label
 return m[1],int(m[2]),int(m[3],16)
running=ota_line('Running'); assert running==ota_line('Boot')
sessions=pre.split('sessions\n',1)[1].split('ota status',1)[0]
assert not re.search(r'\bwriter\b|\bsketch\b',sessions,re.I),sessions
assert not (archive/'full-flash-before.bin').exists(),'Fresh backup/archive required'
lock=base.parent/'papermono-COM39-flash.lock'
fd=os.open(lock,os.O_CREAT|os.O_EXCL|os.O_WRONLY)
os.write(fd,f'pid={os.getpid()} writer_d_flash={archive}\n'.encode())
report=dict(result='RUNNING',port='COM39',implementation_commit=manifest['implementation_commit'],
            artifact_size=len(candidate),artifact_sha256=sha(candidate),write_started=False,
            protected_regions={},trial_mode='baseline',measured_D_updates=0)
esp=None
try:
 esp=detect_chip('COM39',115200)
 assert esp.CHIP_NAME=='ESP32-S3'
 mac=':'.join(f'{v:02x}' for v in esp.read_mac())
 assert mac=='28:84:85:44:79:68',mac
 esp=esp.run_stub();esp.change_baud(460800);attach_flash(esp)
 flash_id=esp.flash_id();assert 1<<((flash_id>>16)&0xff)==0x1000000
 report.update(mac=mac,flash_id=hex(flash_id),flash_size=0x1000000)
 print('BACKUP_START: identified ESP32-S3 / MAC / 16MB',flush=True)
 chunks=[]
 for offset in range(0,0x1000000,0x100000):
  value=read_flash(esp,offset,0x100000,no_progress=True)
  assert len(value)==0x100000;chunks.append(value)
  print(f'BACKUP_PROGRESS {len(chunks)}/16 MiB',flush=True)
 backup=b''.join(chunks)
 with (archive/'full-flash-before.bin').open('xb') as f:
  f.write(backup);f.flush();os.fsync(f.fileno())
 assert (archive/'full-flash-before.bin').read_bytes()==backup
 report['full_backup_sha256']=sha(backup)
 partitions={}
 for pos in range(0x8000,0x9000,32):
  if backup[pos:pos+2]!=b'\xaa\x50':break
  _,kind,subtype,offset,size,label,flags=struct.unpack('<HBBII16sI',backup[pos:pos+32])
  name=label.split(b'\0')[0].decode()
  assert name not in partitions and offset+size<=len(backup)
  partitions[name]=dict(kind=kind,subtype=subtype,offset=offset,size=size)
 table=(base/'artifacts/partitions-reference-only.bin').read_bytes()
 assert backup[0x8000:0x8000+len(table)]==table,'Partition table differs: do not write'
 for name,part in manifest['partitions'].items():
  assert (partitions[name]['offset'],partitions[name]['size'])==(part['offset'],part['size'])
 active,slot,address=running;p=partitions[active]
 assert p['kind']==0 and p['subtype']==0x10+slot and p['offset']==address
 assert len(candidate)<p['size'] and ((len(candidate)+4095)//4096)*4096<=p['size']
 ota=partitions['otadata'];records=[]
 for pos in (ota['offset'],ota['offset']+4096):
  sequence=struct.unpack_from('<I',backup,pos)[0]
  state,crc=struct.unpack_from('<II',backup,pos+24)
  valid=crc==(zlib.crc32(backup[pos:pos+4],0xffffffff)&0xffffffff)
  records.append(dict(sequence=sequence,state=state,crc_valid=valid))
 valid=[r for r in records if r['crc_valid'] and r['sequence'] not in (0,0xffffffff) and r['state'] not in (3,4)]
 assert valid and (max(valid,key=lambda r:r['sequence'])['sequence']-1)%2==slot
 rollback=backup[address:address+len(known)]
 assert rollback==known,'Unexpected current Application: no write'
 with (archive/'application-rollback.bin').open('xb') as f:f.write(rollback)
 assert (archive/'application-rollback.bin').read_bytes()==known
 (archive/'application-partition-before.bin').write_bytes(backup[address:address+p['size']])
 regions={'bootloader':(0,0x8000),'partition_table':(0x8000,0x1000)}
 for name in ('nvs','otadata','phy_init','flash'):
  region=partitions[name];regions[name]=(region['offset'],region['size'])
 for name,region in partitions.items():
  if region['kind']==0 and name!=active:regions[name]=(region['offset'],region['size'])
 for name,(offset,size) in regions.items():
  assert address+p['size']<=offset or offset+size<=address
  if size<0x300000:(archive/f'{name}-prewrite.bin').write_bytes(backup[offset:offset+size])
 report.update(partitions=partitions,ota_records=records,active_slot=active,slot_index=slot,
               app_offset=hex(address),app_partition_size=p['size'],rollback_sha256=sha(rollback),
               rollback_size=len(rollback),protected_region_ranges={n:[o,s] for n,(o,s) in regions.items()})
 (archive/'backup-audit.json').write_text(json.dumps(dict(report,result='BACKUP_PASS'),indent=2),encoding='utf-8')
 assert read_flash(esp,0x8000,0x7000,no_progress=True)==backup[0x8000:0xf000]
 assert read_flash(esp,address,len(rollback),no_progress=True)==rollback
 print(f'PREFLIGHT_PASS: {active} @ {address:#x}; fresh rollback / partition / OTA / SHA match',flush=True)
 report['write_started']=True
 (archive/'write-started.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
 write_flash(esp,[(address,candidate)],flash_size='keep',compress=True,no_progress=True)
 actual=read_flash(esp,address,len(candidate),no_progress=True)
 report['readback_sha256']=sha(actual);assert actual==candidate
 print('APPLICATION_READBACK_PASS',flush=True)
 for name,(offset,size) in regions.items():
  # Bounded reads provide progress during inactive-slot protection verification.
  values=[]
  for relative in range(0,size,0x100000):
   values.append(read_flash(esp,offset+relative,min(0x100000,size-relative),no_progress=True))
   if size>0x100000:print(f'PROTECT_PROGRESS {name} {min(relative+0x100000,size)}/{size}',flush=True)
  value=b''.join(values)
  report['protected_regions'][name]=dict(unchanged=value==backup[offset:offset+size],sha256=sha(value))
  assert value==backup[offset:offset+size],name
  print(f'PROTECTED_REGION_PASS {name}',flush=True)
 report['result']='PASS'
 reset_chip(esp,'hard-reset')
 try:
  esp._port.timeout=0.1
  with (archive/'boot.log').open('xb') as log:
   until=time.monotonic()+8
   while time.monotonic()<until:
    data=esp._port.read(esp._port.in_waiting or 1)
    if data:log.write(data);log.flush()
 except Exception as boot_error:
  report['boot_capture_error']=repr(boot_error) # USB re-enumeration is checked separately.
except BaseException as error:
 report.update(result='FAILED',error=repr(error))
 raise
finally:
 (archive/'flash-summary.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
 if esp is not None:esp._port.close()
 os.close(fd);lock.unlink()
 print(json.dumps(report,indent=2),flush=True)
