#!/usr/bin/env python3
"""Retain an active SM procedure on duplicate encryption initiation.

Build-local ESP-IDF 5.5.5 overlay. The installed SDK and keys are never changed.
"""
import argparse
import hashlib
from pathlib import Path
import re

HOST = Path('components/bt/host/nimble/nimble/nimble/host/src')
SHA256 = '384e25aae3ea1f623aef7a6f10cefe5ed7bbeb972f2ee163a6c6ed3ba2e01a7e'

def fix(source):
    match = re.search(r'\nble_sm_enc_initiate\([^;]*?\n\{[\s\S]*?^}\n', source, re.M)
    if not match:
        raise ValueError('NimBLE encryption function anchor mismatch')
    old = '''        res.app_status = BLE_HS_EALREADY;

        /* Set pointer to null so that existing entry doesn't get freed. */
        proc = NULL;'''
    new = '''        /* A peer Security Request may already have started encryption.
         * Do not pass EALREADY to process_result: it re-finds this connection's
         * active procedure and removes it on any nonzero app_status, losing
         * the later HCI completion's ENC_CHANGE callback and bond metadata. */
        ble_hs_unlock();
        return BLE_HS_EALREADY;'''
    if match[0].count(old) != 1:
        raise ValueError('NimBLE EALREADY anchor mismatch')
    return source[:match.start()] + match[0].replace(old,new,1) + source[match.end():]

def transform(raw):
    if hashlib.sha256(raw).hexdigest() != SHA256:
        raise ValueError('Unsupported security SDK source; expected ESP-IDF 5.5.5. Review and retest the overlay.')
    return fix(raw.decode('utf-8'))

def generate(idf, output):
    idf, output = idf.resolve(), output.resolve()
    if output.is_relative_to(idf):
        raise ValueError('Security overlay must not modify installed SDK')
    rendered = transform((idf / HOST / 'ble_sm.c').read_bytes())
    output.mkdir(parents=True, exist_ok=True)
    target=output/'ble_sm.c'
    if target.is_symlink(): raise ValueError('Security overlay output cannot be a symlink')
    if not target.exists() or target.read_text(encoding='utf-8') != rendered:
        target.write_text(rendered,encoding='utf-8')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--idf',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    generate(args.idf,args.output)
