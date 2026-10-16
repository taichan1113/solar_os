#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
targets="epd_ssd1677_dirty_hold_test epd_ssd1677_test
epd_ssd1677_spi_clock_dirty_20_test epd_ssd1677_hybrid_trial_test
sketch_dirty_pipeline_test sketch_canvas_test sketch_lifecycle_test
sketch_scale140_lifecycle_test sketch_legacy_lifecycle_test
sketch_touch_projection_test sketch_trace_core_test display_async_test
writer_quality_test writer_buffer_test writer_files_test
launcher_touch_test type_mouse_engine_test type_mouse_lifecycle_test shell_launch_test"
# CJSON_DIR may be supplied when the ESP-IDF package is on a Windows mount.
make -C tests/host $targets
for target in $targets; do
    printf 'RUN %s\n' "$target"
    "./tests/host/$target"
done
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest -v tests.test_sketch_trace 2>&1
