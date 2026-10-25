$ErrorActionPreference = "Stop"
# Build only. No device, port selection, upload or file-system image operations.
$env:SOLAR_OS_BOARD = "m5paper_mono"
$env:SOLAR_OS_FLAVOR = "writerdeck"
$env:SOLAR_OS_LAYOUT = "ota"
$env:SOLAR_OS_SSD1677_WRITER_D = "1"
$env:SOLAR_OS_SSD1677_SPI20_TRIAL = "1"
$env:SOLAR_OS_SSD1677_DIRTY_HOLD_TRIAL = "1"
$env:SOLAR_OS_SSD1677_HYBRID_TRIAL = "0"
$env:SOLAR_OS_SSD1677_WAVEFORM_TRIAL = "0"
$env:SOLAR_OS_SKETCH_LATENCY_TRIAL = "0"
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -B -m platformio run -e esp32_s3_devkitc1_n16r8
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Get-FileHash -Algorithm SHA256 -LiteralPath .pio/build/esp32_s3_devkitc1_n16r8/firmware.bin
