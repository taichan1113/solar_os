from pathlib import Path
import os,subprocess,time,json,sys
base=Path(__file__).resolve().parent;wt=base/'worktree';env=os.environ.copy()
for key in ('SOLAR_OS_FLAVOR_FILE','SOLAR_OS_FLAVOR_NAME_OVERRIDE'):env.pop(key,None)
flags={'SOLAR_OS_BOARD':'m5paper_mono','SOLAR_OS_FLAVOR':'writerdeck','SOLAR_OS_LAYOUT':'ota',
       'SOLAR_OS_SSD1677_WAVEFORM_TRIAL':'0','SOLAR_OS_SSD1677_HYBRID_TRIAL':'1',
       'SOLAR_OS_SKETCH_LATENCY_TRIAL':'0'}
# Exercise PaperMono's Integration defaults, without opt-in feature flags.
for key in list(env):
    if key.startswith('SOLAR_OS_SSD1677_BLACK_HOLD_') or key in (
        'SOLAR_OS_SSD1677_WRITER_D_TRIAL','SOLAR_OS_SSD1677_WRITER_D',
        'SOLAR_OS_SSD1677_DIRTY_HOLD_TRIAL','SOLAR_OS_SSD1677_SPI20_TRIAL'):
        env.pop(key)
env.update(flags);args=[sys.executable,'-m','platformio','run','-e','esp32_s3_devkitc1_n16r8']
commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=wt,text=True).strip()
assert not subprocess.check_output(['git','diff','--','src','scripts','tests'],cwd=wt,text=True).strip()
start=time.monotonic()
with (base/'build.log').open('xb') as log:r=subprocess.run(args,cwd=wt,env=env,stdout=log,stderr=subprocess.STDOUT)
assert commit==subprocess.check_output(['git','rev-parse','HEAD'],cwd=wt,text=True).strip()
result={'returncode':r.returncode,'elapsed_seconds':time.monotonic()-start,'command':args,'cwd':str(wt),
        'requested_environment':flags,'source_commit':commit,'device_accessed':False,'flash_performed':False}
(base/'build-result.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(result,indent=2),flush=True);raise SystemExit(r.returncode)
