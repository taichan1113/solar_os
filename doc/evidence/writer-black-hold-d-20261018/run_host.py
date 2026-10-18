from pathlib import Path
import subprocess,json,sys
base=Path(__file__).resolve().parent
wt=base/'worktree'
group=sys.argv[1]
if group=='main':
 command=['env','CJSON_DIR=/mnt/c/Users/takec/.platformio/packages/framework-espidf/components/json/cJSON',
          'sh','tests/host/run_dirty_sketch_integration.sh']
else:
 targets=['writer_d_driver_test','writer_d_async_test','writer_d_shell_test','epd_ssd1677_waveform_trial_test',
          'input_test','input_action_match_test','hid_keyboard_report_test','ble_keyboard_scan_policy_test',
          'ble_keyboard_reconnect_test','ble_service_test','ble_multi_peer_test','ble_nimble_test','ble_hid_test','ble_server_test']
 command=['env','CJSON_DIR=/mnt/c/Users/takec/.platformio/packages/framework-espidf/components/json/cJSON','sh','-c',
          'set -eu\nmake -C tests/host '+' '.join(targets)+'\n'+
          '\n'.join('echo RUN '+t+'\n./tests/host/'+t for t in targets)+
          '\n./tests/host/writer_d_async_test restore-fail\n']
wsl_path='/mnt/e/ESP32/M5PapserMono-SolarOS/.research/writer-black-hold-d/worktree'
with (base/f'host-{group}.log').open('wb') as f:
 r=subprocess.run(['wsl.exe','-d','Ubuntu-24.04','--cd',wsl_path,'--',*command],stdout=f,stderr=subprocess.STDOUT)
(base/f'host-{group}-result.json').write_text(json.dumps({'returncode':r.returncode,'command':command,'device_accessed':False},indent=2),encoding='utf-8')
print(group,r.returncode);raise SystemExit(r.returncode)
