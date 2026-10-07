#!/usr/bin/env python3
"""Generate optional, build-local security instrumentation for ESP-IDF 5.5.5.

No key bytes or packet payloads are recorded. No installed SDK files are edited.
"""
import argparse
import hashlib
from pathlib import Path
import re

HOST = Path('components/bt/host/nimble/nimble/nimble/host/src')
HASHES = {
    'ble_sm.c': '384e25aae3ea1f623aef7a6f10cefe5ed7bbeb972f2ee163a6c6ed3ba2e01a7e',
    'ble_gap.c': '0ad365b2a423b54e9ee4365cdb52a0ffefe4d70be144e36288cc50781f85fe5b',
    'ble_sm_cmd.c': '26b7a069cdf2ace873b5b85d3d775a84900c4cc2c96e7df4ca597e49459cf0ba',
}

def once(source, old, new):
    if source.count(old) != 1:
        raise ValueError('security trace anchor mismatch: ' + old[:80])
    return source.replace(old, new, 1)

def function(source, name, old, new):
    match = re.search(r'\n' + name + r'\([^;]*?\n\{[\s\S]*?^}\n', source, re.M)
    if not match:
        raise ValueError('security trace function mismatch: ' + name)
    body = once(match[0], old, new)
    return source[:match.start()] + body + source[match.end():]

def transform(name, raw, apply_fix=False):
    if hashlib.sha256(raw).hexdigest() != HASHES[name]:
        raise ValueError('Unsupported security trace SDK source: ' + name)
    s = raw.decode('utf-8')
    if name == 'ble_sm.c' and apply_fix:
        from patch_nimble_security import fix
        s = fix(s)
    s = once(s, '#include <string.h>', '#include <string.h>\n#include "solar_os_ble_security_trace.h"')
    if name == 'ble_sm.c':
        s = once(s, '#if NIMBLE_BLE_CONNECT', '''#define SM_TRACE(label, conn, pointer, a, b) do { \
    const struct ble_sm_proc *trace_proc = (pointer); \
    solar_os_ble_security_trace_record((label), (conn), (uintptr_t)trace_proc, \
        trace_proc ? trace_proc->state : 0, trace_proc ? trace_proc->flags : 0, (a), (b)); \
} while (0)
#if NIMBLE_BLE_CONNECT''')
        s = function(s, 'ble_sm_insert', '    STAILQ_INSERT_HEAD',
                     '    SM_TRACE("sm_insert", proc->conn_handle, proc, 0, 0);\n    STAILQ_INSERT_HEAD')
        s = function(s, 'ble_sm_proc_remove', '    if (prev == NULL)',
                     '    SM_TRACE("sm_remove", proc->conn_handle, proc, 0, 0);\n    if (prev == NULL)')
        s = function(s, 'ble_sm_process_result', '        if (proc != NULL) {',
                     '        SM_TRACE("sm_result", conn_handle, proc, res ? res->app_status : -1,\n'
                     '                 res ? (res->enc_cb | (res->execute << 1)) : 0);\n        if (proc != NULL) {')
        s = function(s, 'ble_sm_enc_initiate', '    if (proc != NULL) {',
                     '    SM_TRACE("enc_initiate", conn_handle, proc, key_size, auth);\n    if (proc != NULL) {')
        s = function(s, 'ble_sm_pair_initiate', '    ble_hs_unlock();\n\n    if (proc != NULL) {',
                     '    SM_TRACE("pair_initiate", conn_handle, proc, 0, 0);\n'
                     '    ble_hs_unlock();\n\n    if (proc != NULL) {')
        s = function(s, 'ble_sm_sec_req_rx', '    cmd = (struct ble_sm_sec_req *)(*om)->om_data;',
                     '    cmd = (struct ble_sm_sec_req *)(*om)->om_data;\n'
                     '    SM_TRACE("peer_sec_request", conn_handle, NULL, cmd->authreq, 0);')
        s = function(s, 'ble_sm_sec_req_rx', '    if (res->app_status == 0) {',
                     '    SM_TRACE("peer_lookup", conn_handle, NULL, res->app_status,\n'
                     '        res->app_status ? 0 : (value_sec.ltk_present | (value_sec.key_size << 8) |\n'
                     '            (value_sec.authenticated << 16) | (value_sec.sc << 17)));\n'
                     '    if (res->app_status == 0) {')
        s = function(s, 'ble_sm_start_encrypt_tx',
                     '    return ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,\n'
                     '                                        BLE_HCI_OCF_LE_START_ENCRYPT),\n'
                     '                             &cmd, sizeof(cmd), NULL, 0);',
                     '    SM_TRACE("hci_encrypt_tx", params->connection_handle, NULL, 0, 0);\n'
                     '    int rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,\n'
                     '                                        BLE_HCI_OCF_LE_START_ENCRYPT),\n'
                     '                             &cmd, sizeof(cmd), NULL, 0);\n'
                     '    SM_TRACE("hci_encrypt_status", params->connection_handle, NULL, rc, 0);\n    return rc;')
        s = function(s, 'ble_sm_enc_event_rx', '    if (proc != NULL) {',
                     '    SM_TRACE("hci_encrypt_rx", conn_handle, proc, evt_status, encrypted);\n    if (proc != NULL) {')
        s = function(s, 'ble_sm_rx', '    rx_cb = ble_sm_dispatch_get(op);',
                     '    SM_TRACE("smp_rx", conn_handle, NULL, op, 0);\n    rx_cb = ble_sm_dispatch_get(op);')
    elif name == 'ble_gap.c':
        s = function(s, 'ble_gap_security_initiate', '    STATS_INC(ble_gap_stats, security_initiate);',
                     '    solar_os_ble_security_trace_record("local_sec_request", conn_handle, 0, 0, 0, 0, 0);\n'
                     '    STATS_INC(ble_gap_stats, security_initiate);')
        s = function(s, 'ble_gap_security_initiate', '        if (rc == 0 && value_sec.ltk_present) {',
                     '        solar_os_ble_security_trace_record("local_lookup", conn_handle, 0, 0, 0, rc,\n'
                     '            rc ? 0 : (value_sec.ltk_present | (value_sec.key_size << 8) |\n'
                     '                (value_sec.authenticated << 16) | (value_sec.sc << 17)));\n'
                     '        if (rc == 0 && value_sec.ltk_present) {')
        s = function(s, 'ble_gap_security_initiate', 'done:\n',
                     'done:\n    solar_os_ble_security_trace_record("local_sec_result", conn_handle, 0, 0, 0, rc, 0);\n')
        s = function(s, 'ble_gap_enc_event', '    memset(&event, 0, sizeof event);',
                     '    solar_os_ble_security_trace_record("gap_enc_emit", conn_handle, 0, 0, 0, status,\n'
                     '        security_restored | (bonded << 1));\n    memset(&event, 0, sizeof event);')
    else:
        s = function(s, 'ble_sm_tx', '    STATS_INC(ble_l2cap_stats, sm_tx);',
                     '    uint8_t trace_opcode = 0;\n    (void)os_mbuf_copydata(txom, 0, 1, &trace_opcode);\n'
                     '    solar_os_ble_security_trace_record("smp_tx", conn_handle, 0, 0, 0, trace_opcode, 0);\n'
                     '    STATS_INC(ble_l2cap_stats, sm_tx);')
        s = function(s, 'ble_sm_tx', '    return rc;',
                     '    solar_os_ble_security_trace_record("smp_tx_result", conn_handle, 0, 0, 0, rc, 0);\n    return rc;')
    return s

def generate(idf, output, apply_fix=False):
    idf, output = idf.resolve(), output.resolve()
    if output.is_relative_to(idf):
        raise ValueError('Trace output must not be inside installed SDK')
    rendered = {n: transform(n, (idf / HOST / n).read_bytes(), apply_fix) for n in HASHES}
    output.mkdir(parents=True, exist_ok=True)
    for name, content in rendered.items():
        target = output / name
        if target.is_symlink():
            raise ValueError('Trace output must not use symlinks')
        if not target.exists() or target.read_text(encoding='utf-8') != content:
            target.write_text(content, encoding='utf-8')

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--idf', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--apply-fix',action='store_true')
    args = p.parse_args()
    generate(args.idf, args.output, args.apply_fix)
