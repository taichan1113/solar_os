#!/usr/bin/env python3
"""Compile the actual SDK SM functions with a minimal host state-machine shim."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

def extract(source, name):
    match = re.search(r'\n(?:static )?(?:int|void)\n' + name + r'\([^;]*?\n\{[\s\S]*?^}\n', source, re.M)
    if not match:
        raise ValueError('SDK function not found: ' + name)
    return match[0]

def run(source_path, compiler, output=None, failure_sequence=False):
    root = Path(__file__).resolve().parents[1]
    source = source_path.read_text(encoding='utf-8')
    functions = ''.join(extract(source, n) for n in ('ble_sm_process_result','ble_sm_enc_initiate','ble_sm_enc_event_rx'))
    shim = (root/'tests/host/nimble_security_lifecycle_fixture.c').read_text(encoding='utf-8')
    with tempfile.TemporaryDirectory() as directory:
        tmp = Path(directory)
        cfile, binary = tmp/'test.c', tmp/'test'
        cfile.write_text(shim.replace('/* SDK_FUNCTIONS */',functions),encoding='utf-8')
        subprocess.run([compiler,'-std=c11','-O2','-Wall','-Wextra','-Werror',str(cfile),'-o',str(binary)],check=True)
        # Do not generate a core dump for the expected before-fix failure.
        def no_core():
            import resource
            resource.setrlimit(resource.RLIMIT_CORE,(0,0))
        argv = [str(binary)] + (['--failure-sequence'] if failure_sequence else [])
        result = subprocess.run(argv,capture_output=True,text=True,preexec_fn=no_core)
        text = result.stdout + result.stderr
        if output: output.write_text(text,encoding='utf-8')
        print(text,end='')
        return result.returncode

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,required=True)
    p.add_argument('--compiler',default='gcc')
    p.add_argument('--output',type=Path)
    p.add_argument('--failure-sequence',action='store_true')
    args=p.parse_args()
    raise SystemExit(run(args.source,args.compiler,args.output,args.failure_sequence))
