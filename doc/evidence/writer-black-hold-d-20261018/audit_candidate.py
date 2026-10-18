from pathlib import Path
import hashlib,json,re,struct,subprocess,shutil
base=Path(__file__).resolve().parent;wt=base/'worktree';build=wt/'.pio/build/esp32_s3_devkitc1_n16r8'
load=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
sha=lambda b:hashlib.sha256(b).hexdigest()
def git(*args):return subprocess.check_output(['git','-C',str(wt),*args],text=True).strip()
def lut(text,name):
 body=re.search(r'\b'+name+r'\[110\]\s*=\s*\{(.*?)\};',text,re.S)[1]
 body=re.sub(r'/\*.*?\*/|//[^\n]*','',body,flags=re.S)
 return bytes(int(x,0) for x in re.findall(r'0x[0-9a-fA-F]+|\b[0-9]+\b',body))
parent='391925abff2d261d1d5856711aa0a1f101ac435a'
assert not git('diff','--','src','scripts','tests')
assert not git('diff',parent,'--','src/board','boards','platformio.ini','partitions.csv',
               'src/apps/solar_os_sketch.c','src/apps/solar_os_type_mouse.c',
               'src/drivers/epd_ssd1677_waveform_trial.inc')
ref=(wt/'src/drivers/epd_ssd1677_reference.inc').read_text()
a=lut(ref,'ssd1677_reference_lut');d=lut(ref,'ssd1677_trial_black_hold_d_lut')
original=lut(git('show',parent+':src/drivers/epd_ssd1677_reference.inc'),'ssd1677_reference_lut')
assert a==original and sha(a)=='e8a7abbb2110cb8d941e80cc2869bbe1d0814e1d4a38e5d0cc5173b10f39ce50'
assert sha(d)=='9be4bea8b9a07fef33834293d25979a9b0f3fc820d3e9912a1fc106532ef1832'
assert a[2:]==d[2:] and d[:2]==bytes.fromhex('2ff0')
r=load(base/'build-result.json');log=(base/'build.log').read_text(errors='replace')
assert r['returncode']==0 and '[SUCCESS]' in log and '(5.5.5)' in log
assert git('rev-parse','HEAD')==r['source_commit']
warnings=[x for x in log.splitlines() if re.search(r'\bwarning:|CMake Warning',x,re.I)]
errors=[x for x in log.splitlines() if re.search(r'\berror:|CMake Error|Traceback',x,re.I)]
assert not errors
commands=load(build/'compile_commands.json')
flags={name:1 for name in ('WRITER_D','DIRTY_HOLD','SPI20','HYBRID')}
for file in ('epd_ssd1677.c','solar_os_writer.c','solar_os_ssd1677.c','solar_os_display_async.c','solar_os_shell_commands.c'):
 command=next(x['command'] for x in commands if x['file'].replace(chr(92),'/').endswith('/'+file))
 for name,value in flags.items():assert '-DSOLAR_OS_SSD1677_'+name+'_TRIAL='+str(value) in command,(file,name)
 assert '-DSOLAR_OS_SSD1677_WAVEFORM_TRIAL=1' not in command
 assert '-DSOLAR_OS_SKETCH_LATENCY_TRIAL=1' not in command
 assert '-DSOLAR_OS_SSD1677_BLACK_HOLD_' not in command
selection=(build/'generated/solar_os/platformio_build_selection.txt').read_text()
for s in ('board=m5paper_mono','flavor=writerdeck','layout=ota','ssd1677_writer_d_trial=1','ssd1677_waveform_trial=0'):
 assert s in selection,s
parts=(build/'partitions.bin').read_bytes();partitions={}
for pos in range(0,len(parts),32):
 if parts[pos:pos+2]!=b'\xaa\x50':break
 _,kind,sub,offset,size,label,flags_=struct.unpack('<HBBII16sI',parts[pos:pos+32])
 partitions[label.split(b'\0')[0].decode()]={'offset':offset,'size':size}
assert partitions['ota_0']=={'offset':0x10000,'size':0x700000}
image=(build/'firmware.bin').read_bytes()
assert image[0]==0xe9 and len(image)<0x700000 and a in image and d in image
assert b'writer-lut' in image and b'black-hold-dad' not in image and b'black-hold-single' not in image
for group in ('main','extra'):assert load(base/f'host-{group}-result.json')['returncode']==0
rollback=(base.parent/'dirty-sketch-integration/artifacts-typemouse/firmware-app-only.bin').read_bytes()
assert sha(rollback)=='278665c184653f06a403bd56713e7bf5073131bead0420b3fd2191fa54b12ef6'
sdk=(build/'config/sdkconfig.h').read_text()
assert '#define CONFIG_FREERTOS_HZ 1000' in sdk
out=base/'artifacts';out.mkdir(exist_ok=True)
shutil.copyfile(build/'firmware.bin',out/'firmware-app-only.bin')
shutil.copyfile(build/'partitions.bin',out/'partitions-reference-only.bin')
(out/'build-selection.txt').write_text(selection,encoding='utf-8')
manifest={'result':'WRITER_D_HOST_BUILD_PASS_AWAITING_SEPARATE_FLASH_APPROVAL',
 'branch':git('branch','--show-current'),'base_commit':parent,'implementation_commit':r['source_commit'],
 'implementation_commits':git('rev-list','--reverse',parent+'..'+r['source_commit']).splitlines(),
 'board':'m5paper_mono','flavor':'writerdeck','layout':'ota','esp_idf':'5.5.5','compiler':'GCC 14.2.0',
 'flags':{**{k+'_TRIAL':v for k,v in flags.items()},'WAVEFORM_TRIAL':0,'SKETCH_LATENCY_TRIAL':0},
 'application':'firmware-app-only.bin','application_bytes':len(image),'sha256':sha(image),
 'ota_slot_bytes':0x700000,'ota_remaining_bytes':0x700000-len(image),'partitions':partitions,
 'baseline_LUT_sha256':sha(a),'D_LUT_sha256':sha(d),'unchanged_LUT_indices':[2,109],
 'default_mode':'baseline','D_budget_per_boot':6,'budget_refundable':False,'NVS_modified':False,
 'source_is_explicit_snapshot_metadata':True,'physical_mode_selected_at_execution':True,
 'forced_full_frame_diagnostic_imported':False,'no_op_still_skipped':True,
 'baseline_restore_additional_cmd20':0,'writer_trace_capacity':32,
 'host_major_executables':19,'host_sketch_python_tests':9,'host_extra_executables':14,
 'host_restore_failure_extra_process':True,'build_seconds':r['elapsed_seconds'],
 'final_build_warning_count':len(warnings),'final_build_warnings':warnings,'final_build_error_count':len(errors),
 'initial_build_failure':'Writer macro collision, fixed in source commit; original failure log retained',
 'device_accessed':False,'COM_enumerated':False,'flash_performed':False,'physical_quality_tested':False,
 'Writer_classification':'W-1..W-6 not measured','effective_DC_balance_proven':False,
 'manufacturer_validated':False,'rollback_reference_commit':parent,'rollback_reference_sha256':sha(rollback)}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
print(json.dumps(manifest,indent=2))
