from pathlib import Path
import os,subprocess,time,json,sys
base=Path(__file__).resolve().parent; wt=base/'worktree'; env=os.environ.copy()
for key in ('SOLAR_OS_FLAVOR_FILE','SOLAR_OS_FLAVOR_NAME_OVERRIDE'):
    env.pop(key,None)
flags={'SOLAR_OS_BOARD':'m5paper_mono','SOLAR_OS_FLAVOR':'writerdeck','SOLAR_OS_LAYOUT':'ota','SOLAR_OS_SSD1677_BLACK_HOLD_SINGLE_TRIAL':'1','SOLAR_OS_SSD1677_BLACK_HOLD_REPEAT3_TRIAL':'1','SOLAR_OS_SSD1677_WAVEFORM_TRIAL':'0','SOLAR_OS_SSD1677_HYBRID_TRIAL':'1','SOLAR_OS_SSD1677_DIRTY_HOLD_TRIAL':'1','SOLAR_OS_SSD1677_SPI20_TRIAL':'1','SOLAR_OS_SKETCH_LATENCY_TRIAL':'0'}
env.update(flags)
args=[sys.executable,'-m','platformio','run','-e','esp32_s3_devkitc1_n16r8']
start=time.monotonic()
with (base/'build.log').open('xb') as log:
    r=subprocess.run(args,cwd=wt,env=env,stdout=log,stderr=subprocess.STDOUT)
result={'returncode':r.returncode,'elapsed_seconds':time.monotonic()-start,'command':args,'cwd':str(wt),'requested_environment':flags,'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=wt,text=True).strip(),'device_accessed':False,'flash_performed':False}
(base/'build-result.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(result,indent=2),flush=True)
raise SystemExit(r.returncode)
