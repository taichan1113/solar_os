from pathlib import Path
import hashlib,json,re,struct,subprocess,shutil
base=Path(__file__).resolve().parent;wt=base/'worktree';build=wt/'.pio/build/esp32_s3_devkitc1_n16r8'
load=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
sha=lambda b:hashlib.sha256(b).hexdigest()
def git(*args):return subprocess.check_output(['git','-C',str(wt),*args],text=True,encoding='utf-8').strip()
def lut(text,name):
 body=re.search(r'\b'+name+r'\[110\]\s*=\s*\{(.*?)\};',text,re.S)[1]
 body=re.sub(r'/\*.*?\*/|//[^\n]*','',body,flags=re.S)
 return bytes(int(x,0) for x in re.findall(r'0x[0-9a-fA-F]+|\b[0-9]+\b',body))
parent='391925abff2d261d1d5856711aa0a1f101ac435a'
assert not git('diff','--','src','scripts','tests')
assert not git('diff',parent,'--','src/board','boards','platformio.ini','partitions.csv',
               'src/apps/solar_os_sketch.c','src/apps/solar_os_type_mouse.c',
               'src/drivers/epd_ssd1677_waveform_trial.inc','src/services/solar_os_ble_keyboard.c')
ref=(wt/'src/drivers/epd_ssd1677_reference.inc').read_text(encoding='utf-8')
a=lut(ref,'ssd1677_reference_lut');d=lut(ref,'ssd1677_trial_black_hold_d_lut')
assert a==lut(git('show',parent+':src/drivers/epd_ssd1677_reference.inc'),'ssd1677_reference_lut')
assert sha(a)=='e8a7abbb2110cb8d941e80cc2869bbe1d0814e1d4a38e5d0cc5173b10f39ce50'
assert sha(d)=='9be4bea8b9a07fef33834293d25979a9b0f3fc820d3e9912a1fc106532ef1832'
assert a[2:]==d[2:] and d[:2]==bytes.fromhex('2ff0')
r=load(base/'build-result.json');log=(base/'build.log').read_text(encoding='utf-8',errors='replace')
assert r['returncode']==0 and '[SUCCESS]' in log and '(5.5.5)' in log
assert not git('diff',r['source_commit'],'HEAD','--','src','scripts','tests','README.md')
assert all(name not in r['requested_environment'] for name in ('SOLAR_OS_SSD1677_WRITER_D','SOLAR_OS_SSD1677_SPI20_TRIAL','SOLAR_OS_SSD1677_DIRTY_HOLD_TRIAL'))
warnings=[x for x in log.splitlines() if re.search(r'\bwarning:|CMake Warning',x,re.I)]
errors=[x for x in log.splitlines() if re.search(r'\berror:|CMake Error|Traceback',x,re.I)]
assert not errors
commands=load(build/'compile_commands.json')
flags={'SOLAR_OS_SSD1677_WRITER_D':1,'SOLAR_OS_SSD1677_DIRTY_HOLD_TRIAL':1,
       'SOLAR_OS_SSD1677_SPI20_TRIAL':1,'SOLAR_OS_SSD1677_HYBRID_TRIAL':1}
for file in ('epd_ssd1677.c','solar_os_writer.c','solar_os_ssd1677.c','solar_os_display_async.c','solar_os_shell_commands.c'):
 command=next(x['command'] for x in commands if x['file'].replace(chr(92),'/').endswith('/'+file))
 for name,value in flags.items():assert '-D'+name+'='+str(value) in command,(file,name)
 assert '-DSOLAR_OS_SSD1677_WAVEFORM_TRIAL=1' not in command
 assert '-DSOLAR_OS_SKETCH_LATENCY_TRIAL=1' not in command
 assert 'SOLAR_OS_SSD1677_WRITER_D_TRIAL' not in command
selection=(build/'generated/solar_os/platformio_build_selection.txt').read_text(encoding='utf-8')
for s in ('board=m5paper_mono','flavor=writerdeck','layout=ota','ssd1677_writer_d=1',
          'ssd1677_dirty_hold_trial=1','ssd1677_spi20_trial=1','ssd1677_waveform_trial=0'):assert s in selection,s
code=(wt/'src/drivers/epd_ssd1677_writer_d.inc').read_text(encoding='utf-8')
assert all(token not in code for token in ('SSD1677_WRITER_D_LIMIT','cap_reached','cap_transition','writer_d_attempts'))
assert 'baseline_selected' in code and 'UINT64_MAX' in code
parts=(build/'partitions.bin').read_bytes();partitions={}
for pos in range(0,len(parts),32):
 if parts[pos:pos+2]!=b'\xaa\x50':break
 _,kind,sub,offset,size,label,flags_=struct.unpack('<HBBII16sI',parts[pos:pos+32])
 partitions[label.split(b'\0')[0].decode()]={'offset':offset,'size':size}
assert partitions['ota_0']=={'offset':0x10000,'size':0x700000}
image=(build/'firmware.bin').read_bytes()
assert image[0]==0xe9 and len(image)<0x700000 and a in image and d in image
assert b'writer-lut' in image and b'black-hold-dad' not in image
assert b'Writer D limit=6' not in image
assert b'scope=writer' in image and b'no cumulative limit' in image
for group in ('main','extra'):assert load(base/f'host-{group}-result.json')['returncode']==0
rollback=(base.parent/'dirty-sketch-integration/artifacts-typemouse/firmware-app-only.bin').read_bytes()
assert sha(rollback)=='278665c184653f06a403bd56713e7bf5073131bead0420b3fd2191fa54b12ef6'
out=base/'artifacts';out.mkdir(exist_ok=True)
shutil.copyfile(build/'firmware.bin',out/'firmware-app-only.bin')
shutil.copyfile(build/'partitions.bin',out/'partitions-reference-only.bin')
(out/'build-selection.txt').write_text(selection,encoding='utf-8')
manifest={'result':'CONTINUOUS_WRITER_D_HOST_BUILD_PASS','source_branch':git('branch','--show-current'),
 'base_integration_commit':parent,'implementation_commit':r['source_commit'],
 'board':'m5paper_mono','flavor':'writerdeck','layout':'ota','esp_idf':'5.5.5','compiler':'GCC 14.2.0',
 'effective_flags':{**flags,'SOLAR_OS_SSD1677_WAVEFORM_TRIAL':0,'SOLAR_OS_SKETCH_LATENCY_TRIAL':0},
 'PaperMono_defaults_verified_without_opt_in':True,'default_Writer_mode':'D','other_origins':'Baseline A',
 'cumulative_D_limit':None,'periodic_D_thinning':False,'counters':'saturating uint64 diagnostic only',
 'counter_saturation_does_not_stop_D':True,'initial_cold_fastest_uses_D':True,'initial_OTP_unchanged':True,
 'baseline_restore_extra_cmd20':0,'no_op_and_queue_coalescing_preserved':True,
 'application':'firmware-app-only.bin','application_bytes':len(image),'sha256':sha(image),
 'ota_slot_bytes':0x700000,'ota_remaining_bytes':0x700000-len(image),'partitions':partitions,
 'baseline_LUT_sha256':sha(a),'D_LUT_sha256':sha(d),'LUT_bytes_2_through_109_unchanged':True,
 'Host_major_executables':19,'Sketch_Python_tests':9,'Host_extra_executables':14,
 'Host_default_and_updates_1_6_7_20_21_D':True,'Host_async_through_D_23':True,
 'Host_Baseline_switch_and_restore_fault_PASS':True,'Host_RAM_Shadow_R0_R2_retain_swap_PASS':True,
 'build_seconds':r['elapsed_seconds'],'final_build_warning_count':len(warnings),'final_build_warnings':warnings,
 'final_build_error_count':len(errors),'COM_enumerated':False,'device_accessed':False,'flash_performed':False,
 'physical_continuous_Writer_D_verified':False,'long_term_DC_lifetime_verified':False,
 'known_practical_rollback_commit':parent,'known_practical_rollback_sha256':sha(rollback)}
integration_head=git('rev-parse','integration/papermono-dirty-sketch')
assert not git('diff',r['source_commit'],integration_head,'--','src','scripts','tests','README.md')
subprocess.run(['git','merge-base','--is-ancestor',r['source_commit'],integration_head],cwd=wt,check=True)
manifest.update(integration_reflected=True,integration_branch='integration/papermono-dirty-sketch',
                integration_source_commit=r['source_commit'],
                integration_worktree=str(base.parent/'dirty-sketch-integration/worktree'))
(out/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
print(json.dumps(manifest,indent=2))
