from pathlib import Path
import os
import sys
import time
import serial

base = Path(__file__).resolve().parent
import json
archive = Path(json.loads((base/'active-flash-archive.json').read_text(encoding='utf-8'))['archive'])
archive.mkdir(parents=True, exist_ok=True)
name = sys.argv[1]
target = archive / name
assert not target.exists(), target
port = serial.Serial()
port.port, port.baudrate, port.timeout = 'COM39', 115200, 0.1
port.dtr = port.rts = False
lock=base.parent / 'papermono-COM39-flash.lock'
fd=os.open(lock,os.O_CREAT | os.O_EXCL | os.O_WRONLY)
os.write(fd,f'pid={os.getpid()} continuous_writer_d_usb_query\n'.encode())
try:
    port.open()
    with target.open('wb') as log:
        def capture(seconds):
            end = time.monotonic() + seconds
            while time.monotonic() < end:
                data = port.read(port.in_waiting or 1)
                if data:
                    log.write(data)
                    log.flush()
        capture(1)
        for command in sys.argv[2:]:
            port.write(command.encode() + b'\r')
            port.flush()
            capture(3)
finally:
    port.close()
    os.close(fd)
    lock.unlink()
print(target.read_text(encoding='utf-8', errors='replace'))
