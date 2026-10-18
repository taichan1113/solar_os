from pathlib import Path
import hashlib,json,re,struct,subprocess,shutil
base=Path(__file__).resolve().parent;wt=base/'worktree';build=wt/'.pio/build/esp32_s3_devkitc1_n16r8'
load=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
sha=lambda b:hashlib.sha256(b).hexdigest()
def git(*args):return subprocess.check_output(['git','-C',str(wt),*args],encoding='utf-8').strip()
parent='655d61359b3a0d285da2d6e8a949e023168d3d62'
assert not git('diff','--','src','scripts','tests','README.md')
changed=set(git('diff','--name-only',parent,'--','src/apps','src/services').splitlines())
assert changed <= {'src/apps/solar_os_sketch.c','src/services/solar_os_writer_lut_trial.h','src/services/solar_os_display.c','src/services/solar_os_display_async.c','src/services/solar_os_gfx.c','src/services/solar_os_gfx.h'},changed
assert not git('diff',parent,'--','scripts','boards','platformio.ini','partitions.csv')
def lut(text,name):
 body=re.search(r'\b'+name+r'\[110\]\s*=\s*\{(.*?)\};',text,re.S)[1]
 body=re.sub(r'/\*.*?\*/|//[^\n]*','',body,flags=re.S)
 return bytes(int(x,0) for x in re.findall(r'0x[0-9a-fA-F]+|\b[0-9]+\b',body))
ref=(wt/'src/drivers/epd_ssd1677_reference.inc').read_text(encoding='utf-8')
a=lut(ref,'ssd1677_reference_lut');d=lut(ref,'ssd1677_trial_black_hold_d_lut')
prior=git('show',parent+':src/drivers/epd_ssd1677_reference.inc')
assert a==lut(prior,'ssd1677_reference_lut') and d==lut(prior,'ssd1677_trial_black_hold_d_lut')
assert sha(a)=='e8a7abbb2110cb8d941e80cc2869bbe1d0814e1d4a38e5d0cc5173b10f39ce50'
assert sha(d)=='9be4bea8b9a07fef33834293d25979a9b0f3fc820d3e9912a1fc106532ef1832'
assert a[2:]==d[2:] and d[:2]==bytes.fromhex('2ff0')
code=(wt/'src/drivers/epd_ssd1677_writer_d.inc').read_text(encoding='utf-8')
selector=code.split('static bool writer_d_select',1)[1].split('static void writer_d_finish',1)[0]
assert 'origin.is_writer' not in selector and 'origin.is_sketch' in selector
assert all(token not in selector for token in ('LIMIT','attempts >=','attempts >'))
assert 'd->refresh_mode != EPD_SSD1677_REFRESH_FASTEST' in selector
r=load(base/'build-result.json');log=(base/'build.log').read_text(encoding='utf-8',errors='replace')
assert r['returncode']==0 and '[SUCCESS]' in log and '(5.5.5)' in log
assert not git('diff',r['source_commit'],'HEAD','--','src','scripts','tests','README.md')
warnings=[x for x in log.splitlines() if re.search(r'\bwarning:|CMake Warning',x,re.I)]
errors=[x for x in log.splitlines() if re.search(r'\berror:|CMake Error|Traceback',x,re.I)]
assert not errors
commands=load(build/'compile_commands.json')
flags={'SOLAR_OS_SSD1677_WRITER_D':1,'SOLAR_OS_SSD1677_DIRTY_HOLD_TRIAL':1,
       'SOLAR_OS_SSD1677_SPI20_TRIAL':1,'SOLAR_OS_SSD1677_HYBRID_TRIAL':1}
for file in ('epd_ssd1677.c','solar_os_writer.c','solar_os_ssd1677.c','solar_os_shell_commands.c'):
 command=next(x['command'] for x in commands if x['file'].replace(chr(92),'/').endswith('/'+file))
 for name,value in flags.items():assert '-D'+name+'='+str(value) in command
 assert '-DSOLAR_OS_SSD1677_WAVEFORM_TRIAL=1' not in command
 assert '-DSOLAR_OS_SKETCH_LATENCY_TRIAL=1' not in command
assert '#define SOLAR_OS_BOARD_SKETCH_ASYNC_PRESENT 1' in (build/'generated/solar_os/solar_os_board_generated.h').read_text(encoding='utf-8')
assert 'solar_os_gfx_submit_sketch' in (build/'firmware.map').read_text(encoding='utf-8',errors='replace')
parts=(build/'partitions.bin').read_bytes();partitions={}
for pos in range(0,len(parts),32):
 if parts[pos:pos+2]!=b'\xaa\x50':break
 _,kind,sub,offset,size,label,_=struct.unpack('<HBBII16sI',parts[pos:pos+32])
 partitions[label.split(b'\0')[0].decode()]={'offset':offset,'size':size}
assert partitions['ota_0']=={'offset':0x10000,'size':0x700000}
image=(build/'firmware.bin').read_bytes();assert image[0]==0xe9 and len(image)<0x700000
assert a in image and d in image and b'scope=all-apps-except-sketch' in image and b'fastest-lut' in image
assert b'black-hold-dad' not in image
for group in ('main','extra'):assert load(base/f'host-{group}-result.json')['returncode']==0
for text in ('All-app physical update 7','All-app physical update 20','All-app physical update 21',
             'real synchronous u8g2 callback','all-app continuous through D update 24',
             'Baseline restore failure stops worker','Sketch exception:','Sketch origin survives actual Executor:'):
 assert text in (base/'host-extra.log').read_text(encoding='utf-8'),text
out=base/'artifacts';out.mkdir(exist_ok=True)
shutil.copyfile(build/'firmware.bin',out/'firmware-app-only.bin')
shutil.copyfile(build/'partitions.bin',out/'partitions-reference-only.bin')
selection=(build/'generated/solar_os/platformio_build_selection.txt').read_text(encoding='utf-8')
(out/'build-selection.txt').write_text(selection,encoding='utf-8')
manifest={'result':'ALL_APP_D_SKETCH_BASELINE_HOST_BUILD_PASS','source_branch':git('branch','--show-current'),
 'base_integration_commit':parent,'implementation_commit':r['source_commit'],
 'board':'m5paper_mono','flavor':'writerdeck','layout':'ota','esp_idf':'5.5.5','compiler':'GCC 14.2.0',
 'effective_flags':{**flags,'SOLAR_OS_SSD1677_WAVEFORM_TRIAL':0,'SOLAR_OS_SKETCH_LATENCY_TRIAL':0},
 'default_mode':'D','scope':'all-apps-except-sketch','Sketch_policy':'Baseline A','refresh_scope':'fastest','cumulative_D_limit':None,
 'periodic_D_thinning':False,'Writer_origin_is_diagnostic_only':True,'Sketch_origin_is_Driver_exception':True,'sync_and_async_shared_lifecycle':True,
 'application':'firmware-app-only.bin','application_bytes':len(image),'sha256':sha(image),
 'ota_slot_bytes':0x700000,'ota_remaining_bytes':0x700000-len(image),'partitions':partitions,
 'baseline_LUT_sha256':sha(a),'D_LUT_sha256':sha(d),'LUT_bytes_unchanged_from_Writer_Integration':True,
 'Host_major_executables':19,'Sketch_Python_tests':9,'Host_extra_executables':15,
 'Host_default_and_updates_1_6_7_20_21_D':True,'Host_async_mixed_sources_through_D_24':True,
 'Host_OTP_modes_unchanged_and_cold_fastest_PASS':True,'Host_sync_u8g2_fault_noop_PASS':True,'Host_Sketch_UI_Canvas_tag_lifecycle_PASS':True,'Host_Sketch_A_other_D_Executor_PASS':True,
 'Host_RAM_Shadow_R0_R2_retain_swap_PASS':True,'Host_global_mode_and_restore_failure_PASS':True,
 'build_seconds':r['elapsed_seconds'],'build_warning_count':len(warnings),'build_warnings':warnings,
 'build_error_count':len(errors),'physical_all_app_continuous_quality_verified':False,
 'long_term_DC_lifetime_verified':False}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
print(json.dumps(manifest,indent=2))
