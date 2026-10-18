from pathlib import Path
import hashlib,json,re,struct,subprocess,shutil
base=Path(__file__).resolve().parent;wt=base/'worktree';build=wt/'.pio/build/esp32_s3_devkitc1_n16r8';out=base/'artifacts'
load=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
sha=lambda b:hashlib.sha256(b).hexdigest()
def git(*args):return subprocess.check_output(['git','-C',str(wt),*args],text=True).strip()
parent='42aec97cb19db5b920b3d29a8923e3a2e2724737'
allowed={'src/CMakeLists.txt','src/drivers/epd_ssd1677.c','src/drivers/epd_ssd1677.h','src/shell/solar_os_shell_black_hold_single.inc','src/shell/solar_os_shell_commands.c'}
assert set(git('diff',parent,'--name-only','--','src').splitlines())==allowed
assert not git('diff',parent,'--','src/apps','src/services','src/board','boards','platformio.ini','partitions.csv','src/drivers/epd_ssd1677_reference.inc','src/drivers/epd_ssd1677_waveform_trial.inc')
assert not git('diff','--','src','scripts','tests')
def lut(text,name):
    body=re.search(r'\b'+name+r'\[110\]\s*=\s*\{(.*?)\};',text,re.S)[1]
    body=re.sub(r'/\*.*?\*/|//[^\n]*','',body,flags=re.S)
    return bytes(int(x,0) for x in re.findall(r'0x[0-9a-fA-F]+|\b[0-9]+\b',body))
ref=(wt/'src/drivers/epd_ssd1677_reference.inc').read_text(encoding='utf-8')
trial=(wt/'src/drivers/epd_ssd1677_waveform_trial.inc').read_text(encoding='utf-8')
a=lut(ref,'ssd1677_reference_lut');d=lut(trial,'ssd1677_trial_black_hold_d_lut');b=lut(trial,'ssd1677_trial_black_hold_b_lut')
assert sha(a)=='e8a7abbb2110cb8d941e80cc2869bbe1d0814e1d4a38e5d0cc5173b10f39ce50'
assert sha(d)=='9be4bea8b9a07fef33834293d25979a9b0f3fc820d3e9912a1fc106532ef1832'
assert a[:10]==bytes(10) and d[:10]==bytes.fromhex('2ff0')+bytes(8) and a[2:]==d[2:]
result=load(base/'build-result.json');log=(base/'build.log').read_text(encoding='utf-8',errors='replace')
assert result['returncode']==0 and '[SUCCESS]' in log and '(5.5.5)' in log
warnings=[x for x in log.splitlines() if re.search(r'\bwarning:|CMake Warning',x,re.I)]
errors=[x for x in log.splitlines() if re.search(r'\berror:|CMake Error|Traceback',x,re.I)]
assert not warnings and not errors,(warnings,errors)
command=next(x['command'] for x in load(build/'compile_commands.json') if x['file'].replace(chr(92),'/').endswith('/epd_ssd1677.c'))
flags={name:1 for name in ('BLACK_HOLD_SINGLE','BLACK_HOLD_REPEAT3','BLACK_HOLD_PRIME_AA','DIRTY_HOLD','SPI20','HYBRID')}
for name,value in flags.items():assert '-DSOLAR_OS_SSD1677_'+name+'_TRIAL='+str(value) in command
assert '-DSOLAR_OS_SSD1677_WAVEFORM_TRIAL=1' not in command
assert '-DSOLAR_OS_SKETCH_LATENCY_TRIAL=1' not in command
selection=(build/'generated/solar_os/platformio_build_selection.txt').read_text(encoding='utf-8')
for value in ('board=m5paper_mono','flavor=writerdeck','layout=ota','ssd1677_black_hold_prime_aa_trial=1','ssd1677_waveform_trial=0'):assert value in selection
parts=(build/'partitions.bin').read_bytes();assert parts==(base.parent/'black-hold-d-repeat3/artifacts/partitions-reference-only.bin').read_bytes()
partitions={}
for pos in range(0,len(parts),32):
    if parts[pos:pos+2]!=b'\xaa\x50':break
    _,kind,sub,offset,size,label,flags_=struct.unpack('<HBBII16sI',parts[pos:pos+32]);partitions[label.split(b'\0')[0].decode()]={'offset':offset,'size':size}
assert partitions['ota_0']=={'offset':0x10000,'size':0x700000}
image=(build/'firmware.bin').read_bytes();assert image[0]==0xe9 and len(image)<0x700000 and a in image and d in image
assert b not in image and bytes.fromhex('bf00')+a[2:] not in image
for value in (b'black-hold-d-prime-aa',b'BH schedule condition=DAA',b'BH interval condition=DAA',b'BH timing phase=',b'physical_readback=0',b'2FF0',b'0000'):assert value in image,value
assert b'black-hold-repeat3 A|D' not in image and b'black-hold-single B|D' not in image
config=lambda p:{line for line in p.read_text(encoding='utf-8').splitlines() if line.startswith('#define CONFIG_')}
assert config(build/'config/sdkconfig.h')==config(base.parent/'black-hold-d-repeat3/worktree/.pio/build/esp32_s3_devkitc1_n16r8/config/sdkconfig.h')
assert '#define CONFIG_FREERTOS_HZ 1000' in config(build/'config/sdkconfig.h')
rollback=(base.parent/'dirty-sketch-integration/artifacts-typemouse/firmware-app-only.bin').read_bytes()
assert sha(rollback)=='278665c184653f06a403bd56713e7bf5073131bead0420b3fd2191fa54b12ef6'
for name in ('host-single-result.json','host-regressions-result.json'):assert load(base/name)['returncode']==0
out.mkdir(exist_ok=True);shutil.copyfile(build/'firmware.bin',out/'firmware-app-only.bin');shutil.copyfile(build/'partitions.bin',out/'partitions-reference-only.bin')
(out/'build-selection.txt').write_text(selection,encoding='utf-8')
manifest={'result':'PRIME_AA_HOST_BUILD_PASS_AWAITING_EXPLICIT_FLASH_APPROVAL','branch':git('branch','--show-current'),
 'base_commit':parent,'implementation_commit':result['source_commit'],'board':'m5paper_mono','flavor':'writerdeck','layout':'ota',
 'esp_idf':'5.5.5','compiler':'GCC 14.2.0','flags':{**{k+'_TRIAL':v for k,v in flags.items()},'WAVEFORM_TRIAL':0,'SKETCH_LATENCY_TRIAL':0},
 'application':'firmware-app-only.bin','application_bytes':len(image),'sha256':sha(image),'ota_slot_bytes':0x700000,
 'ota_remaining_bytes':0x700000-len(image),'partitions':partitions,'baseline_LUT_sha256':sha(a),'D_LUT_sha256':sha(d),
 'LUT_voltage_physical_refresh_files_unchanged':True,'normal_apps_services_board_unchanged':True,
 'B_C_waveforms_absent_in_binary':True,'old_single_repeat3_commands_not_registered':True,
 'fixed_measurement_sequence':['D','A','A'],'max_counted_attempts':3,'max_D_attempts':1,'max_A_attempts':2,
 'cancel_terminates_sequence':True,'session_restart_allowed':False,'budget_persists_display_reinitialization':True,
 'budget_survives_whole_device_reboot':False,'trial_persisted_in_NVS_startup':False,
 'timing_ms':{'T0_after_preparation':5000,'end_to_next_start_goal':60000,'minimum_after_operator_approval':5000,'Tn_after_completion':2000},
 'planned_diagnostic_cmd20':10,'planned_measured_cmd20':3,'planned_D_cmd20':1,'intermediate_OTP_between_measurements':False,
 'sdk_config_equal_to_previous_repeat3':True,'build_seconds':result['elapsed_seconds'],'compiler_warning_count':len(warnings),'compiler_error_count':len(errors),
 'device_accessed':False,'COM_enumerated':False,'flash_performed':False,'physical_quality_tested':False,'P_classification':'not measured',
 'effective_DC_balance_proven':False,'manufacturer_validated':False,'rollback_reference_commit':'391925abff2d261d1d5856711aa0a1f101ac435a','rollback_reference_sha256':sha(rollback)}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8');print(json.dumps(manifest,indent=2))
