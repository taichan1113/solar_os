#!/usr/bin/env python3
"""Offline Application-only plan/readback audit. Never opens USB/serial or flashes.

The operator supplies a fresh full Flash read, runtime sessions/OTA log, the
built Application and partition table. All device commands remain manual.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib

def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def make_plan(full: bytes, table: bytes, image: bytes, preflight: str) -> dict:
    if len(full) != 16*1024*1024:
        raise ValueError('Expected a fresh, complete 16 MiB Flash read')
    if len(image)<36 or image[0]!=0xe9 or struct.unpack_from('<I',image,32)[0]!=0xabcd5432:
        raise ValueError('Expected an ESP Application image, not a factory/bootloader image')
    positions=[p for p in range(0x1000,len(full)-len(table)+1,0x1000) if full[p:p+len(table)]==table]
    if len(table)<32 or len(positions)!=1:
        raise ValueError('Built partition table must match exactly once in the device backup')
    table_offset=positions[0]
    partitions={}
    for p in range(0,len(table)-31,32):
        if table[p:p+2]!=b'\xaa\x50': break
        _,kind,subtype,offset,size,label,flags=struct.unpack_from('<HBBII16sI',table,p)
        name=label.split(b'\0')[0].decode('ascii')
        if name in partitions or not size or offset+size>len(full):
            raise ValueError('Invalid partition bounds/label')
        partitions[name]=dict(kind=kind,subtype=subtype,offset=offset,size=size)
    ranges=sorted((v['offset'],v['offset']+v['size']) for v in partitions.values())
    if any(a[1]>b[0] for a,b in zip(ranges,ranges[1:])):
        raise ValueError('Partition ranges overlap')
    if not re.search(r'^\s*\d+\s+\S+\s+shell\s+(active|suspended)\b',preflight,re.M):
        raise ValueError('Runtime sessions listing including Shell is required')
    if re.search(r'^\s*\d+\s+\S+\s+(writer|sketch)\s',preflight,re.M|re.I):
        raise ValueError('Save and close every Writer/Sketch session first')
    def slot(label):
        m=re.search(label+r':\s+(ota_[01]) \(slot ([01])\) addr (0x[0-9a-fA-F]+)',preflight)
        if not m: raise ValueError('Runtime Running/Boot OTA records required')
        return m[1],int(m[2]),int(m[3],16)
    active,index,address=slot('Running')
    if slot('Boot')!=(active,index,address): raise ValueError('Running and Boot slots differ')
    app=partitions.get(active,{})
    if app.get('kind')!=0 or app.get('subtype')!=0x10+index or app.get('offset')!=address:
        raise ValueError('Runtime slot does not match the actual partition table')
    ota_apps=sorted(v['subtype'] for v in partitions.values() if v['kind']==0 and 0x10<=v['subtype']<0x20)
    if ota_apps!=[0x10,0x11]: raise ValueError('Expected exactly two OTA Application slots')
    if (len(image)+4095)//4096*4096>app['size']: raise ValueError('Image/erase sectors exceed Application capacity')
    ota=next((p for p in partitions.values() if p['kind']==1 and p['subtype']==0),None)
    if ota is None or ota['size']<8192: raise ValueError('OTA data not found')
    records=[]
    for p in (ota['offset'],ota['offset']+4096):
        seq=struct.unpack_from('<I',full,p)[0]
        state,crc=struct.unpack_from('<II',full,p+24)
        valid=crc==(zlib.crc32(full[p:p+4],0xffffffff)&0xffffffff)
        records.append(dict(sequence=seq,state=state,crc_valid=valid))
    valid=[r for r in records if r['crc_valid'] and r['sequence'] not in (0,0xffffffff) and r['state'] not in (3,4)]
    if not valid or (max(valid,key=lambda r:r['sequence'])['sequence']-1)%2!=index:
        raise ValueError('Raw otadata does not confirm the runtime slot')
    return dict(result='OFFLINE_PLAN_PASS',flash_bytes=len(full),full_backup_sha256=sha(full),
        app_offset=address,app_partition_size=app['size'],active_slot=active,
        image_bytes=len(image),image_sha256=sha(image),partition_table_offset=table_offset,
        partitions=partitions,ota_records=records,
        rollback_sha256=sha(full[address:address+app['size']]))

def verify(plan: dict, before: bytes, after: bytes, image: bytes) -> dict:
    if sha(before)!=plan['full_backup_sha256'] or sha(image)!=plan['image_sha256']:
        raise ValueError('Backup/image differs from the audited plan')
    address=plan['app_offset']; end=address+plan['app_partition_size']
    if len(after)!=len(before) or after[address:address+len(image)]!=image:
        raise ValueError('Application readback mismatch')
    if before[:address]!=after[:address] or before[end:]!=after[end:]:
        raise ValueError('Protected Flash outside the target Application changed')
    return dict(result='OFFLINE_READBACK_AND_PROTECTION_PASS',image_sha256=sha(image),
                all_bytes_outside_active_application_unchanged=True)

def main() -> None:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('action',choices=('plan','verify'))
    p.add_argument('--before',required=True,type=Path)
    p.add_argument('--image',required=True,type=Path)
    p.add_argument('--partition-table',type=Path)
    p.add_argument('--preflight',type=Path)
    p.add_argument('--after',type=Path)
    args=p.parse_args(); folder=args.before.resolve().parent; plan_path=folder/'application-plan.json'
    before=args.before.read_bytes(); image=args.image.read_bytes()
    if args.action=='plan':
        if not args.partition_table or not args.preflight: p.error('plan requires --partition-table and --preflight')
        if plan_path.exists() or (folder/'application-before.bin').exists():
            raise ValueError('Use a new archive; do not overwrite a previous plan/backup')
        result=make_plan(before,args.partition_table.read_bytes(),image,args.preflight.read_text(encoding='utf-8'))
        address=result['app_offset']; end=address+result['app_partition_size']
        with (folder/'application-before.bin').open('xb') as out: out.write(before[address:end])
        with plan_path.open('x',encoding='utf-8') as out: json.dump(result,out,indent=2)
    else:
        if not args.after: p.error('verify requires --after')
        result=verify(json.loads(plan_path.read_text(encoding='utf-8')),before,args.after.read_bytes(),image)
        with (folder/'application-verify.json').open('x',encoding='utf-8') as out: json.dump(result,out,indent=2)
    print(json.dumps(result,indent=2))

if __name__=='__main__': main()
