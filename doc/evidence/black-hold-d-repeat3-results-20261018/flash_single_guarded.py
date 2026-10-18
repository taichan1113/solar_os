"""One authorized application-only deployment, with fresh backup and readback."""
from pathlib import Path
import hashlib
import json
import os
import struct
import time
import zlib
from esptool.cmds import detect_chip, attach_flash, read_flash, write_flash, reset_chip

base = Path(__file__).resolve().parent
archive = base / 'archive' / '20261018-d-repeat3'
archive.mkdir(parents=True, exist_ok=True)
sha = lambda value: hashlib.sha256(value).hexdigest()
artifact = base / 'artifacts' / 'firmware-app-only.bin'
candidate = artifact.read_bytes()
assert sha(candidate) == '26acd03ca96f608cf3602466c0efd239cdb6f17ac475e317e124c90904af8e4a'
assert len(candidate) == 3460144 and len(candidate) < 0x700000
rollback = (base.parent / 'dirty-sketch-integration' / 'artifacts-typemouse' / 'firmware-app-only.bin').read_bytes()
assert sha(rollback) == '278665c184653f06a403bd56713e7bf5073131bead0420b3fd2191fa54b12ef6'
preflight = (archive / 'preflight-usb.log').read_text(encoding='utf-8')
assert 'Running: ota_0 (slot 0) addr 0x010000' in preflight
assert 'Boot: ota_0 (slot 0) addr 0x010000' in preflight
session_text=preflight.split('sessions\n', 1)[1].split('ota status', 1)[0]
assert 'writer' not in session_text and 'sketch' not in session_text
assert not (archive / 'full-flash-before.bin').exists(), 'Fresh archive required'
regions = dict(bootloader=(0, 0x8000), partition_table=(0x8000, 0x1000),
               nvs=(0x9000, 0x4000), otadata=(0xd000, 0x2000),
               phy_init=(0xf000, 0x1000), fat=(0xe10000, 0x1f0000))
lock = base.parent / 'papermono-COM39-flash.lock'
lock_fd = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
os.write(lock_fd, f'pid={os.getpid()} black_hold_repeat3={base}\n'.encode())
report = dict(result='RUNNING', port='COM39', commit='e1935704df27d6d0e7e5cf25c7d63133c8c74faa',
              app_offset='0x10000', artifact_size=len(candidate), artifact_sha256=sha(candidate),
              write_started=False, protected_regions={})
esp = None
try:
    esp = detect_chip('COM39', 115200)
    assert esp.CHIP_NAME == 'ESP32-S3'
    mac = ':'.join(f'{v:02x}' for v in esp.read_mac())
    assert mac == '28:84:85:44:79:68', mac
    esp = esp.run_stub()
    esp.change_baud(460800)
    attach_flash(esp)
    flash_id = esp.flash_id()
    assert 1 << ((flash_id >> 16) & 0xff) == 0x1000000
    report.update(mac=mac, flash_id=hex(flash_id), flash_size=0x1000000)
    print('BACKUP_START: identified ESP32-S3, MAC and 16MB flash', flush=True)
    chunks = []
    for offset in range(0, 0x1000000, 0x100000):
        data = read_flash(esp, offset, 0x100000, no_progress=True)
        assert len(data) == 0x100000
        chunks.append(data)
        print(f'BACKUP_PROGRESS {len(chunks)}/16 MiB', flush=True)
    backup = b''.join(chunks)
    with (archive / 'full-flash-before.bin').open('xb') as handle:
        handle.write(backup)
        handle.flush()
        os.fsync(handle.fileno())
    assert (archive / 'full-flash-before.bin').read_bytes() == backup
    report['full_backup_sha256'] = sha(backup)
    partitions = {}
    for pos in range(0x8000, 0x9000, 32):
        if backup[pos:pos+2] != b'\xaa\x50':
            break
        _, kind, subtype, offset, size, label, flags = struct.unpack('<HBBII16sI', backup[pos:pos+32])
        partitions[label.split(b'\0')[0].decode()] = dict(kind=kind, subtype=subtype, offset=offset, size=size)
    table = (base / 'artifacts' / 'partitions-reference-only.bin').read_bytes()
    assert backup[0x8000:0x8000+len(table)] == table
    for name, (offset, size) in dict(nvs=(0x9000,0x4000), otadata=(0xd000,0x2000),
                                    phy_init=(0xf000,0x1000), ota_0=(0x10000,0x700000),
                                    ota_1=(0x710000,0x700000), flash=(0xe10000,0x1f0000)).items():
        assert (partitions[name]['offset'], partitions[name]['size']) == (offset, size)
    records = []
    for pos in (0xd000, 0xe000):
        sequence = struct.unpack_from('<I', backup, pos)[0]
        state, crc = struct.unpack_from('<II', backup, pos+24)
        valid = crc == (zlib.crc32(backup[pos:pos+4], 0xffffffff) & 0xffffffff)
        records.append(dict(sequence=sequence, state=state, crc_valid=valid))
    valid_records = [r for r in records if r['crc_valid'] and r['sequence'] not in (0,0xffffffff)
                     and r['state'] not in (3,4)]
    assert valid_records and (max(valid_records, key=lambda r:r['sequence'])['sequence']-1) % 2 == 0
    assert backup[0x10000:0x10000+len(rollback)] == rollback, 'Unexpected running application: no write'
    with (archive / 'ota0-app-rollback.bin').open('xb') as handle:
        handle.write(rollback)
    assert (archive / 'ota0-app-rollback.bin').read_bytes() == rollback
    for name, (offset, size) in regions.items():
        (archive / f'{name}-prewrite.bin').write_bytes(backup[offset:offset+size])
    report.update(partitions=partitions, ota_records=records, active_slot='ota_0',
                  rollback_sha256=sha(rollback), rollback_size=len(rollback))
    (archive / 'backup-audit.json').write_text(json.dumps(dict(report, result='BACKUP_PASS'), indent=2), encoding='utf-8')
    assert read_flash(esp, 0x8000, 0x7000, no_progress=True) == backup[0x8000:0xf000]
    assert read_flash(esp, 0x10000, len(rollback), no_progress=True) == rollback
    print('PREFLIGHT_PASS: fresh rollback, partition, OTA0 and candidate SHA verified', flush=True)
    report['write_started'] = True
    write_flash(esp, [(0x10000, candidate)], flash_size='keep', compress=True, no_progress=True)
    readback = read_flash(esp, 0x10000, len(candidate), no_progress=True)
    report['readback_sha256'] = sha(readback)
    assert readback == candidate
    print('APPLICATION_READBACK_PASS', flush=True)
    for name, (offset, size) in regions.items():
        value = read_flash(esp, offset, size, no_progress=True)
        report['protected_regions'][name] = dict(unchanged=value == backup[offset:offset+size], sha256=sha(value))
        assert value == backup[offset:offset+size], name
        print(f'PROTECTED_REGION_PASS {name}', flush=True)
    report['result'] = 'PASS'
    reset_chip(esp, 'hard-reset')
except BaseException as error:
    report.update(result='FAILED', error=repr(error))
    raise
finally:
    (archive / 'flash-summary.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    if esp is not None:
        esp._port.close()
    os.close(lock_fd)
    lock.unlink()
    print(json.dumps(report, indent=2), flush=True)
