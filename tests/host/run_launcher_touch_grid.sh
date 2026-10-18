#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
sh tests/host/run_dirty_sketch_integration.sh
targets="launcher_grid_test app_touch_test files_layout_test reader_touch_test
clock_touch_test terminal_render_scale140_test ft6336_lifecycle_test
input_touch_adapter_test input_touch_service_test button_long_press_test rtc_service_test
m5papermono_power_test sketch_baseline_lifecycle_test writer_d_driver_test
writer_d_async_test writer_d_shell_test epd_ssd1677_waveform_trial_test
input_test input_action_match_test hid_keyboard_report_test ble_keyboard_scan_policy_test
ble_keyboard_reconnect_test ble_service_test ble_multi_peer_test ble_nimble_test
ble_hid_test ble_server_test"
make -C tests/host $targets
for target in $targets; do
    printf 'RUN %s\n' "$target"
    "./tests/host/$target"
done
./tests/host/writer_d_async_test restore-fail
