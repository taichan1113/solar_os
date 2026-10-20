#!/usr/bin/env bash
# Build only. Does not open USB or invoke any PlatformIO upload target.
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
solaros_python=${SOLAROS_PYTHON:-"$HOME/.platformio/penv/bin/python"}
unset SOLAR_OS_FLAVOR_FILE SOLAR_OS_FLAVOR_NAME_OVERRIDE SOLAR_OS_SSD1677_WRITER_D_TRIAL
export SOLAR_OS_BOARD=m5paper_mono SOLAR_OS_FLAVOR=writerdeck SOLAR_OS_LAYOUT=ota
export SOLAR_OS_SSD1677_WRITER_D=1 SOLAR_OS_SSD1677_DIRTY_HOLD_TRIAL=1
export SOLAR_OS_SSD1677_SPI20_TRIAL=1 SOLAR_OS_SSD1677_HYBRID_TRIAL=1
export SOLAR_OS_SSD1677_WAVEFORM_TRIAL=0 SOLAR_OS_SKETCH_LATENCY_TRIAL=0
export SOLAR_OS_SKETCH_LUT_PANEL_TRIAL=1
"$solaros_python" -m platformio run -e esp32_s3_devkitc1_n16r8
solaros_build=.pio/build/esp32_s3_devkitc1_n16r8
stat -c '%s bytes %n' "$solaros_build/firmware.bin" "$solaros_build/partitions.bin"
sha256sum "$solaros_build/firmware.bin" "$solaros_build/partitions.bin"
