from pathlib import Path
import subprocess,json,sys
base=Path('.research/black-hold-d-repeat3'); kind=sys.argv[1]
if kind=='single':
    args=['sh','.research/black-hold-d-repeat3/run_single_host.sh']; log=base/'host-single-verified.log'
else:
    args=['env','CJSON_DIR=/mnt/c/Users/takec/.platformio/packages/framework-espidf/components/json/cJSON','sh','.research/black-hold-d-repeat3/worktree/tests/host/run_dirty_sketch_integration.sh']; log=base/'host-regressions.log'
with log.open('xb') as f:
    r=subprocess.run(['wsl.exe','-d','Ubuntu-24.04','--cd','/mnt/e/ESP32/M5PapserMono-SolarOS','--',*args],stdout=f,stderr=subprocess.STDOUT)
(base/f'host-{kind}-result.json').write_text(json.dumps({'returncode':r.returncode,'command':args,'log':str(log)},indent=2),encoding='utf-8')
print(kind,'exit',r.returncode)
raise SystemExit(r.returncode)
