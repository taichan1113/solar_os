from pathlib import Path
import json,hashlib,re,subprocess,struct,shutil
base=Path(__file__).resolve().parent;wt=base/'worktree';build=wt/'.pio/build/esp32_s3_devkitc1_n16r8';out=base/'artifacts'
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def git(*a):return subprocess.check_output(['git','-C',str(wt),*a],text=True).strip()
def lut(text,name):
 s=re.search(r'\b'+name+r'\[110\]\s*=\s*\{(.*?)\};',text,re.S)[1];s=re.sub(r'/\*.*?\*/|//[^\n]*','',s,flags=re.S)
 return bytes(int(x,0) for x in re.findall(r'0x[0-9a-fA-F]+|\b[0-9]+\b',s))
sha=lambda b:hashlib.sha256(b).hexdigest()
parent='a65d8ec83b5ae2bff5ace40e8032f047657a0ffd'
allowed={'src/CMakeLists.txt','src/drivers/epd_ssd1677.c','src/drivers/epd_ssd1677.h','src/drivers/epd_ssd1677_reference.inc','src/drivers/epd_ssd1677_waveform_trial.inc','src/shell/solar_os_shell_black_hold_single.inc','src/shell/solar_os_shell_commands.c'}
assert set(git('diff',parent,'--name-only','--','src').splitlines())==allowed
assert not git('diff',parent,'--','src/apps','src/services','src/board','boards','platformio.ini','partitions.csv')
assert not git('diff','--','src','scripts','tests')
reference=(wt/'src/drivers/epd_ssd1677_reference.inc').read_text(encoding='utf-8')
laboratory=(wt/'src/drivers/epd_ssd1677_waveform_trial.inc').read_text(encoding='utf-8')
a=lut(reference,'ssd1677_reference_lut');d=lut(laboratory,'ssd1677_trial_black_hold_d_lut');b=lut(laboratory,'ssd1677_trial_black_hold_b_lut')
assert sha(a)=='e8a7abbb2110cb8d941e80cc2869bbe1d0814e1d4a38e5d0cc5173b10f39ce50'
assert sha(d)=='9be4bea8b9a07fef33834293d25979a9b0f3fc820d3e9912a1fc106532ef1832'
assert sha(b)=='45c5419f6986e3b1a81f8394e386fd5bc627503dcaa6d2c483a9113a63209422'
assert len(a)==len(d)==110 and a[10:]==d[10:]
old_reference=git('show',parent+':src/drivers/epd_ssd1677_reference.inc')
assert reference[reference.index('static esp_err_t ssd1677_reference_refresh'):].strip()==old_reference[old_reference.index('static esp_err_t ssd1677_reference_refresh'):].strip(), 'Normal physical refresh/RAM algorithm changed'
commands=read(build/'compile_commands.json');command=next(x['command'] for x in commands if x['file'].replace(chr(92),'/').endswith('/epd_ssd1677.c'))
for flag in ('BLACK_HOLD_SINGLE','BLACK_HOLD_REPEAT3','DIRTY_HOLD','SPI20','HYBRID'):assert '-DSOLAR_OS_SSD1677_'+flag+'_TRIAL=1' in command
assert '-DSOLAR_OS_SSD1677_WAVEFORM_TRIAL=1' not in command
selection=(build/'generated/solar_os/platformio_build_selection.txt').read_text(encoding='utf-8')
for value in ('board=m5paper_mono','flavor=writerdeck','layout=ota','ssd1677_black_hold_repeat3_trial=1','ssd1677_black_hold_single_trial=1','ssd1677_waveform_trial=0','ssd1677_spi20_trial=1','ssd1677_dirty_hold_trial=1'):assert value in selection,value
result=read(base/'build-result.json');log=(base/'build.log').read_text(encoding='utf-8',errors='replace')
assert result['returncode']==0 and '[SUCCESS]' in log and '(5.5.5)' in log
warnings=[x for x in log.splitlines() if re.search(r'\bwarning:|CMake Warning',x,re.I)];errors=[x for x in log.splitlines() if re.search(r'\berror:|CMake Error|Traceback',x,re.I)]
assert not warnings and not errors,(warnings,errors)
parts=(build/'partitions.bin').read_bytes();assert parts==(base.parent/'black-hold-bd-compare/artifacts/partitions-reference-only.bin').read_bytes()
partitions={}
for pos in range(0,len(parts),32):
 if parts[pos:pos+2]!=b'\xaa\x50':break
 _,kind,sub,offset,size,label,flags=struct.unpack('<HBBII16sI',parts[pos:pos+32]);partitions[label.split(b'\0')[0].decode()]={'offset':offset,'size':size}
assert partitions['ota_0']=={'offset':0x10000,'size':0x700000}
image=(build/'firmware.bin').read_bytes();assert image[0]==0xe9 and len(image)<0x700000 and a in image and d in image
assert b not in image and bytes.fromhex('bf00')+a[2:] not in image
for value in (b'black-hold-repeat3 A|D',b'T3-cleanup',b'%c-repeat-%u',b'completed=0 boot_attempts=%u',b'measured_cmd20=%u',b'ram_bytes=%u'):assert value in image,value
assert b'black-hold-single B|D' not in image and b'B-single' not in image
config=lambda p:{s for s in p.read_text(encoding='utf-8').splitlines() if s.startswith('#define CONFIG_')}
assert config(build/'config/sdkconfig.h')==config(base.parent/'black-hold-bd-compare/worktree/.pio/build/esp32_s3_devkitc1_n16r8/config/sdkconfig.h')
assert '#define CONFIG_FREERTOS_HZ 1000' in config(build/'config/sdkconfig.h')
rollback=(base.parent/'dirty-sketch-integration/artifacts-typemouse/firmware-app-only.bin').read_bytes();assert sha(rollback)=='278665c184653f06a403bd56713e7bf5073131bead0420b3fd2191fa54b12ef6'
out.mkdir(exist_ok=True)
shutil.copyfile(build/'firmware.bin',out/'firmware-app-only.bin');shutil.copyfile(build/'partitions.bin',out/'partitions-reference-only.bin');(out/'build-selection.txt').write_text(selection,encoding='utf-8')
m={'result':'REPEAT3_HOST_BUILD_PASS_AWAITING_EXPLICIT_FLASH_APPROVAL','branch':git('branch','--show-current'),'implementation_commit':result['source_commit'],'base_commit':parent,'board':'m5paper_mono','flavor':'writerdeck','layout':'ota','esp_idf':'5.5.5','compiler':'GCC 14.2.0','flags':{'BLACK_HOLD_SINGLE_TRIAL':1,'BLACK_HOLD_REPEAT3_TRIAL':1,'DIRTY_HOLD_TRIAL':1,'SPI20_TRIAL':1,'HYBRID_TRIAL':1,'WAVEFORM_TRIAL':0,'SKETCH_LATENCY_TRIAL':0},'application':'firmware-app-only.bin','application_bytes':len(image),'sha256':sha(image),'ota_slot_bytes':0x700000,'ota_remaining_bytes':0x700000-len(image),'partitions':partitions,'baseline_LUT_sha256':sha(a),'D_LUT_sha256':sha(d),'LUT_bytes_changed_from_BD':False,'normal_reference_refresh_function_unchanged':True,'B_C_waveforms_absent_in_binary':True,'maximum_boot_attempts':{'A':3,'D':3},'session_restart_allowed':False,'budget_persists_display_reinitialization':True,'budget_survives_whole_device_reboot':False,'candidate_persisted_in_NVS':False,'prepared_custom_excluded_from_measurements':True,'timing_ms':{'T0_from_prepare':5000,'measurement_from_operator_request':5000,'Tn_from_measurement_end':2000},'planned_maximum_diagnostic_cmd20_each':10,'planned_maximum_diagnostic_cmd20_total':20,'sdk_config_equal_to_BD':True,'build_seconds':result['elapsed_seconds'],'compiler_warning_count':len(warnings),'compiler_error_count':len(errors),'effective_DC_balance_proven':False,'manufacturer_validated':False,'physical_quality_tested':False,'device_accessed':False,'flash_performed':False,'runtime_note':'Normal waveform baseline; actual current device/slot/clock not queried. Existing startup selects fastest/tile; Dirty rows-dual retained.','rollback_reference_commit':'391925abff2d261d1d5856711aa0a1f101ac435a','rollback_reference_sha256':sha(rollback)}
(out/'manifest.json').write_text(json.dumps(m,indent=2),encoding='utf-8');print(json.dumps(m,indent=2))
