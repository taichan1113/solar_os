"""Restore the application backed up for this deployment; no data rollback.

Run only after saving documents and confirming USB device ownership.
"""
from pathlib import Path
import hashlib
import json
import os
from esptool.cmds import detect_chip, attach_flash, read_flash, write_flash, reset_chip

base = Path(__file__).resolve().parent
archive = base / 'archive' / '20261018-d-repeat3'
sha = lambda data: hashlib.sha256(data).hexdigest()
audit = json.loads((archive / 'backup-audit.json').read_text(encoding='utf-8'))
backup = (archive / 'full-flash-before.bin').read_bytes()
assert sha(backup) == audit['full_backup_sha256']
image = (archive / 'ota0-app-rollback.bin').read_bytes()
assert sha(image) == audit['rollback_sha256'] == '278665c184653f06a403bd56713e7bf5073131bead0420b3fd2191fa54b12ef6'
candidate = (base / 'artifacts' / 'firmware-app-only.bin').read_bytes()
assert sha(candidate) == '26acd03ca96f608cf3602466c0efd239cdb6f17ac475e317e124c90904af8e4a'
regions = dict(bootloader=(0,0x8000), partition_table=(0x8000,0x1000), nvs=(0x9000,0x4000),
               otadata=(0xd000,0x2000), phy_init=(0xf000,0x1000), fat=(0xe10000,0x1f0000))
report = dict(result='RUNNING', rollback_sha256=sha(image), protected_regions={})
lock = base.parent / 'papermono-COM39-flash.lock'
lock_fd = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
os.write(lock_fd, f'pid={os.getpid()} restore_d_repeat3={base}\n'.encode())
esp = None
try:
    esp = detect_chip('COM39',115200)
    assert esp.CHIP_NAME == 'ESP32-S3'
    assert ':'.join(f'{v:02x}' for v in esp.read_mac()) == audit['mac']
    esp = esp.run_stub()
    esp.change_baud(460800)
    attach_flash(esp)
    assert 1 << ((esp.flash_id() >> 16) & 0xff) == 0x1000000
    current = {n:read_flash(esp,o,s,no_progress=True) for n,(o,s) in regions.items()}
    assert current['partition_table'] == backup[0x8000:0x9000]
    assert current['otadata'] == backup[0xd000:0xf000], 'OTA selection changed: re-evaluate slot first'
    assert read_flash(esp,0x10000,len(candidate),no_progress=True) == candidate
    write_flash(esp,[(0x10000,image)],flash_size='keep',compress=True,no_progress=True)
    assert read_flash(esp,0x10000,len(image),no_progress=True) == image
    for name,(offset,size) in regions.items():
        value=read_flash(esp,offset,size,no_progress=True)
        report['protected_regions'][name]=dict(unchanged=value==current[name],sha256=sha(value))
        assert value==current[name],name
    report['result']='PASS'
    reset_chip(esp,'hard-reset')
except BaseException as error:
    report.update(result='FAILED', error=repr(error))
    raise
finally:
    (archive / 'restore-summary.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    if esp is not None:
        esp._port.close()
    os.close(lock_fd)
    lock.unlink()
    print(json.dumps(report,indent=2),flush=True)
