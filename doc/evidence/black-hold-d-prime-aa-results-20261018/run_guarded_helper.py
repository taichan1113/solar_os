from pathlib import Path
import subprocess,sys
base=Path(__file__).resolve().parent
kind=sys.argv[1]
assert kind in ('flash','restore')
run=base/'archive/20261018-d-prime-aa'
log=run/(kind+'.log')
with log.open('xb') as f:
 r=subprocess.run([sys.executable,str(base/(kind+'_single_guarded.py'))],stdout=f,stderr=subprocess.STDOUT)
print(kind,'exit',r.returncode,'log',log,flush=True)
raise SystemExit(r.returncode)
