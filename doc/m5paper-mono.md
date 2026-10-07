# M5Stack PaperMono port / 実機 bring-up

2026-10-07: KEY1 now uses the standard SolarOS system KEY path (default short
press suspend, long press BLE forget/pairing). KEY2 remains Enter. See the
[Japanese input/BLE acceptance checklist](m5paper-mono-input-acceptance.md) for
the current semantics, exact commands and separate hardware acceptance results.

2026-10-07 fast-refresh follow-up: the official OTP full-frame differential path
has passed standalone physical tests (about 0.86 s versus 4.48 s full, including
repeated toggles and manual cleanup). See the [refresh investigation and current
acceptance record](m5paper-mono-refresh.md) for exact revisions, baseline policy,
timing details and Writer acceptance. Older bring-up entries below are historical.

Status: experimental. COM39 hardware boot, memory, power setup, full refresh,
USB shell, Writer launch and FT6336 attachment have been verified. The user
confirms legible output, correct colors and portrait orientation. The requested
default is landscape with USB-C on the left short edge near its upper end,
matching the user's case reference. Terminal orientation is 270 degrees.
COM39 upload and reboot confirmed that profile and an active Writer session.
The 2026-10-07 system KEY acceptance confirmed physical KEY1 suspend/resume
and KEY2 Enter events (key counter 0 to 2). The user confirmed display resume.
USB Serial/JTAG required cable reconnection after automatic light sleep;
the runtime log survived and USB shell became responsive. Physical KEY1 long
press paired a ZMK travelers-split. Legacy bond data required a backed-up,
NimBLE-namespace-only reset; a shared HID event-order fix then enabled automatic
reconnection after reboot. BLE typing, KEY2 newline, and KEY1 suspend/resume
with the same Writer session and retained text are now verified. Four touch
press/release pairs match the expected landscape corner mapping; GUI icon
hit-testing remains unverified. See the current checklist above.

## Architecture and scope

SolarOS baseline: `1ba98726e0c52f5147c8a565522a94d7b67b0291`.
PlatformIO remains the build entry point, `framework = espidf`, pinned pioarduino
`55.03.312-1` / ESP-IDF 5.5.5. No Arduino dependency or new PlatformIO environment.
Both profiles use the existing `esp32_s3_devkitc1_n16r8` environment and its neutral
N16R8 board. A separate manifest is preferable to inheriting the DevKit's many
exposed GPIOs: PaperMono has permanently wired peripherals on those pins.

The existing manifest compiler generates board metadata and required packages.
The fixed early `m5papermono-power` device owns both I2C addresses, checks M5PM1,
and registers M5IOE1 with SolarOS's GPIO controller/resource services. Only then
can `display0` claim its external reset/power lines. No service-level board
`#ifdef`, independent I2C driver, or duplicate bus initialization is introduced.
The GPIO adapter serializes read/modify/write operations. Consumers exclusively
claim each line; a controller with leased lines cannot detach.

Reused: USB Serial/JTAG CDC, UART, I2C/SPI bus services, SSD1677 U8g2 framebuffer
conversion/display registration, FT6336 sample parsing and pointer events,
physical button input, shell and Writer packages.

New: one fixed PaperMono M5PM1/M5IOE1 adapter, SSD1677 external reset callback and
OTP panel option, and FT6336 external reset/power bindings. SSD1677 and FT6336
`reset` (and SSD1677 `power`) bindings now use existing `gpio_line` syntax;
integer GPIO values remain accepted by manifests and the runtime CLI.

## Hardware / gap analysis

| Function | Wiring / implementation | Status |
| --- | --- | --- |
| MCU / memory | ESP32-S3R8, 16 MB QIO flash, 8 MB octal PSRAM, existing S3 defaults | Configured |
| Internal I2C | SDA47, SCL48, 100 kHz | Existing shared bus service |
| M5PM1 | 0x6e, ID 0x2050, disable idle sleep and watchdog, preserve/enable PWR_CFG bits 0x17 | New fixed adapter |
| M5IOE1 | 0x4f, 14 lines, push-pull output, disable idle sleep | New adapter |
| EPD SPI | MOSI14, CLK15, CS16, DC17, BUSY18 active high; SPI2 at 10 MHz | Existing SSD1677 transport |
| EPD rails | PYG3 enable, PYG5 reset | `power="ioe0:2"`, `reset="ioe0:4"` |
| Display | Native controller 800×480, U8g2 R3 portrait base, terminal +270 -> landscape 800×480 | OTP full monochrome only |
| Touch | FT6336G 0x38, IRQ4, PYG6 reset, PYG13 enable | Existing FT6336 with GPIO-line adapter |
| Buttons | GPIO2 KEY1 -> system KEY (power / BLE); GPIO3 KEY2 -> Enter on release; active low, pull-up | Existing system KEY and button services |
| USB | GPIO19/20 USB Serial/JTAG; monitor 115200 | Existing CDC driver |
| Charger bus mux | PYG11 held low, PWM3 disabled | Internal to power adapter; cannot be driven by clients |
| microSD | CMD12/CLK13/D0=11/D1=10/D2=9/D3=8, PYG14 enable | Reserved, not enabled |
| RTC | RX8130CE 0x32; interrupt via PM1 GPIO0 | Deferred; existing PCF/BM8563 is not a drop-in |
| Frontlight | Documentation says PM1 GPIO3; current M5GFX light class also contains IOE/PWM handling | Deferred pending hardware revision verification |
| Battery / sleep | PM1/charger and wake-source semantics | Deferred |
| NFC / LoRa / IMU / audio / RGB | Pins reserved, no drivers attached | Out of minimum scope |

IOE numbering is **zero-based** in SolarOS bindings: PYG1=0, PYG14=13.
The adapter does not initialize every peripheral or change charging voltage/current.
The official PWR_CFG 0x17 mask does include charge enable. Existing firmware can
leave peripheral state retained; this port is not a whole-board low-power mode.

## Panel differences and authoritative sources

Sources inspected (immutable revisions):

- [SolarOS](https://github.com/nilseuropa/solar_os/tree/1ba98726e0c52f5147c8a565522a94d7b67b0291): README, platformio.ini,
  doc/manual/boards.md, driver catalog, manifests for Waveshare 3.97, X4 Pro,
  Freenove 4.0 and QDtech ES3C28P, SSD1677/FT6336, GPIO controllers and services.
- [PaperMono product documentation](https://docs.m5stack.com/en/core/PaperMono):
  pin tables, OTP recommendation, charging mux caveat and download mode.
- [M5GFX](https://github.com/m5stack/M5GFX/tree/c5a3fefad0b38a52cc750e2c4761e339a69a7208):
  src/M5GFX.cpp PaperMono detection/power/reset and Panel_SSD1677.hpp/.inl.
- [M5IOE1](https://github.com/m5stack/M5IOE1/tree/846eec7d05e25c09013be2acdb8804487f48a62e):
  register definitions, output/drive/pull operations, zero-based pin constants.
- [M5PM1](https://github.com/m5stack/M5PM1/tree/be9a5456c007c333e7ac963f33bfde1ffa5d82ee):
  ID, power, watchdog and I2C sleep registers.
- [UserDemo](https://github.com/m5stack/M5PaperMono-UserDemo/tree/c1099107271d31a0678d661a896e2b04dbb331ea):
  main startup settling delay, hal_board, hal_tf_card, display/touch and sleep code.
- [Official OTP demo](https://github.com/m5stack/M5PaperMono-OTP-Demo/tree/c7c02554f89fd06f80d988b805b2a59050c78a46):
  EDP_SPI hardware reset and EDP_OTP_LUT_demo `init_mono_mode` /
  `refresh_mono_full`. This is the refresh authority for this port.

The product documentation explicitly calls the current M5GFX PaperMono waveforms
unstable and recommends the manufacturer's OTP example. M5GFX's booster ends in
0x40; the newer OTP example uses 0x80. This port follows OTP (0x80, border 0x01).
SolarOS's existing driver uses border 0x80/0xc0 and differential update 0xfc,
including for its full cleanup. That path is retained for existing boards only.

PaperMono uses the documented two-activation full sequence: reset, internal
temperature sensor, booster and gate setup; 0x22=0xf8 with inverted image in
RAM 0x24; wait BUSY; 0x22=0x14 with the same non-inverted image in 0x26 and 0x24;
wait BUSY; deep sleep 0x10=0x01 and 100 ms delay. It does not write custom LUT,
VCOM, gate/source voltage or grayscale settings. PaperMono now also exposes
`refresh=auto` (default), `refresh=partial`, `refresh=full` and a one-shot
`refresh=cleanup` request. Fast uses OTP `22=FF` and a full 48 KB RAM24 transfer.
Unknown baseline falls back to full; identical frames with valid baseline skip.
The board sets `SOLAR_OS_BOARD_WRITER_CURSOR_BLINK=0`: Writer keeps its cursor
visible rather than scheduling a full refresh every 500 ms for blinking. Other
boards retain the existing blinking behavior. Full refresh takes about 4.5 s
on the tested device; the fast sequence takes about 0.86 s in standalone tests.

The proven SolarOS/M5GFX descending-Y address convention (data entry 0x01) is
retained to match its framebuffer/rotation, whereas the OTP demo renders its
quadrants with ascending-Y (0x03). This is an address-coordinate difference,
not a different drive waveform; physical orientation still needs corner testing.
Reset is external, 10 ms low and 10 ms high. BUSY waits are bounded at 30 s.
On a refresh error the display latches a fault and refuses further automatic
refreshes/resumes until reboot. The external EPD supply stays enabled while the
controller sleeps between frames, matching the official OTP example.

Touch reports portrait 480×800 coordinates; its driver stays at `rotation=0`.
Terminal orientation is **relative** to U8g2 R3 and defaults to 270 degrees for
landscape use with USB-C on the left short edge, near its upper end. The
[official front-view case diagram](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1267/C153_PaperMono_main_pictures_01.webp)
places USB-C on the bottom short edge in portrait; rotate that physical device
clockwise 90 degrees to obtain the requested landscape pose. Earlier wording
that placed USB on the upper/lower long edge was incorrect.
Keep manifest display dimensions at the portrait
base 480×800: the existing targeted-pointer dispatcher maps `(x,y)` to
`(799-y,x)` for the 800×480 logical UI. Rotating the touch driver too would apply
the transform twice. Use `setterm orientation 0` for portrait, or 90 for the
opposite landscape direction. Saved NVS preferences override the board default.
The official active touch area is approximately X5–475/Y5–795. Verify near all
four corners after changing orientation.

Licenses: SolarOS Apache-2.0; M5Stack libraries and demos MIT; LovyanGFX panel
code carries a FreeBSD/BSD notice. No M5GFX library or custom LUT is imported.
Register facts and OTP sequence were reimplemented in the existing C driver;
M5Stack MIT notices are retained in `src/drivers/m5paper_mono.LICENSE`.
No secondary Free-Ink/draftling code was copied.

## Build and first flash (PowerShell)

Run from the repository root. This session installed dependencies in `.pio-core`
to avoid changing the user's existing PlatformIO platforms.

```powershell
$env:PLATFORMIO_CORE_DIR = "$PWD\.pio-core"
$env:UV_CACHE_DIR = "$PWD\.pio-core\.cache\uv"
$env:PYTHONIOENCODING = 'utf-8' # esptool progress output on Japanese Windows
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
$python = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
& $python scripts/validate_board_metadata.py
& $python scripts/generate_board_profile.py --manifest boards/manifests/m5paper_mono.toml --manifest-dir boards/manifests --drivers boards/expansion_drivers.toml --validate-only

# First flash: display, power and buttons; no touch attachment.
$env:SOLAR_OS_BOARD = 'm5paper_mono_bringup'
$env:SOLAR_OS_FLAVOR = 'writerdeck'
& $pio run -e esp32_s3_devkitc1_n16r8
& $pio device list
$port = 'COM39' # User-confirmed PaperMono for this session; recheck after reconnecting.
& $pio run -e esp32_s3_devkitc1_n16r8 -t upload --upload-port $port
& $pio device monitor --port $port --baud 115200

# After first full refresh/UI verification, build/flash with touch enabled.
$env:SOLAR_OS_BOARD = 'm5paper_mono'
& $pio run -e esp32_s3_devkitc1_n16r8 -t upload --upload-port $port
& $pio device monitor --port $port --baud 115200
```

Keep both environment variables set on every build/upload invocation. Do not
use an old firmware.bin left in the shared base environment's build directory.
For a core-only diagnostic build set `SOLAR_OS_FLAVOR=core`; return to writerdeck
for Writer. In shells with PlatformIO on PATH, `pio` replaces `& $pio`.

Official download-mode procedure: hold power approximately two seconds until
the red LED blinks, then release. The enumerated COM port may change. Do not
erase the entire flash just to debug a panel problem; existing NVS settings may
need inspection if a saved terminal orientation overrides the new default.

## Expected logs and hardware checks

1. Save the complete serial boot log, starting with ROM/reset information.
   ESP-IDF should identify ESP32-S3, 16 MB flash and 8 MB PSRAM. Verify memory
   detection rather than relying only on build metadata.
2. Expect `papermono-power: stage 1: M5PM1/M5IOE1 settling`, followed by
   `stage 2: M5PM1 ready (ID 2050)` and `stage 3: M5IOE1 ready ... controller=ioe0`.
   A failed PMIC/IOE setup leaves no GPIO provider, so EPD attachment cannot run.
3. `IOE PYG3=1` marks EPD power enable, followed by PYG5 low/high reset.
   Expect `PaperMono SSD1677 init complete: OTP full monochrome`.
4. Expect `PaperMono full refresh starting (OTP F8/14)` and
   `PaperMono full refresh complete; controller sleeping`. The first visible
   result should be the SolarOS shell/UI in landscape black and white, with
   full-screen flashes during changes. This is a slow initial bring-up mode.
5. USB initially carries boot logs only on display boards. Start a port shell
   with `session create shell cdc0 --term dumb --size 80x25` from a BLE keyboard
   or `/.shell/startup` (see below). Then use `status`, `pkg`,
   `expansion devices`, `port list`.
   Confirm fixed `ioe0` and `display0` report board origin. Use shell `help` to
   inspect commands. From USB use `session create writer display0` to start
   the graphical Writer; `writer` by itself targets the USB terminal. Physical KEY2
   sends Enter; KEY1 uses the system power/pairing path. Use a BLE keyboard's Esc
   to exit Writer. These two buttons are not a typing keyboard.
6. With `m5paper_mono`, expect PYG13 power and PYG6 reset then touch0 attached.
   Verify tap/release and coordinate alignment near all four corners; then
   verify UI selection/gestures. USB shell commands provide a diagnostic path;
   USB text is not automatically keyboard input for the display app.
7. If BUSY times out or I2C fails, preserve the **first** failure log and stop
   repeated resets/refresh attempts. Capture USB vs battery power, hardware SKU /
   PCB revision, full boot log, expansion device state, controller mode, a photo
   of panel output, and the exact board/flavor/firmware hash. Reboot only after
   checking the cause. Observe whether it failed before power, reset, init or
   either activation. Compilation cannot validate these physical conditions.

## Next work

After phase 1/2 hardware acceptance: add microSD (prefer existing SDMMC with
IOE-controlled PYG14 power), RX8130CE-specific RTC, verified frontlight, battery /
charging telemetry, then additional sleep/wake coverage. Follow the separate
refresh acceptance record for fast Writer evaluation and manual full cleanup.
No arbitrary periodic cleanup interval is assumed. Storage, battery life,
grayscale, peripheral power
retention and wake-source handling are not acceptance claims of this port.

## Validation record (2026-10-06)

- `validate_board_metadata.py`: all profiles valid.
- `generate_board_profile.py --validate-only`: both PaperMono profiles valid.
- Python regression suite: 537 tests, 533 pass, 4 skip (WSL Ubuntu 24.04,
  Python 3.12.3; `CJSON_DIR` points into `.pio-core/packages/framework-espidf`).
- GCC host tests: SSD1677 including OTP sequence/error latch, M5PM1/M5IOE1
  register adapter, FT6336 external/native reset and sample parsing, X4 Pro
  display provider, and GPIO controller/resource tests passed.
- Final landscape `m5paper_mono` / `writerdeck`: compile/link/bin generation
  successful; application 3,301,011 bytes, reported internal static RAM 52,280 bytes.
  These numbers exclude runtime allocations and do not prove runtime stability.
- Generated sdkconfig: 16 MB flash, octal PSRAM, PSRAM 80 MHz enabled.
- COM39 identified by the user as PaperMono. esptool identifies ESP32-S3 rev 0.2,
  embedded PSRAM 8 MB, MAC `28:84:85:44:79:68`.
- Original flash read: exactly 16,777,216 bytes saved before any upload to
  `.research/hardware/papermono-before-solaros.bin`.
  SHA256 `ba6d200a753914d726b1962cc3af372e633b898345022280241ebdd64cdc59d7`.
- `m5paper_mono_bringup` / `writerdeck`: uploaded with hash verification.
  Hardware logs confirm 16 MB flash, 8 MB PSRAM at 80 MHz and SRAM test OK,
  PM1 ID 2050, IOE UID d75c, three completed full refreshes and shell startup.
- The user confirms displayed content and colors are normal, and subsequently
  confirms portrait text matches the vertically held device (USB at lower left).
  The user then specified landscape with USB-C at the upper part of the left
  side. The final default is R3 + 270 degrees, following that case reference.
- Touch-enabled writerdeck upload and boot succeeded. `touch0` reports ready on
  i2c0 0x38, target 480x800 rotation 0. `input touch` reports an absolute pointer;
  `input buttons` reports the physical-button source. Device registration alone
  does not establish successful physical events or corner accuracy.
- Internal flash initially had no compatible filesystem. Its backed-up data
  partition at 0xe10000 was replaced with an ESP-IDF wear-levelled FAT image.
  Mount at `/`, USB shell commands, and `session create writer display0`
  succeeded. Logs: `.research/hardware/serial-shell.log`, `writer-launch.log`.
- Legacy NimBLE bonding data reports `expected 88, got 80`. NVS was preserved;
  BLE pairing has not been validated. This does not stop the display shell.
- Writer initially caused continuous full refreshes due to its 500 ms blinking
  cursor. After disabling blink in this board profile, 30 seconds of serial
  monitoring showed no idle refresh, with Writer remaining active. See
  `.research/hardware/writer-idle.log` and `final-boot.log`.
- The earlier 90-degree landscape upload passed flash hash verification. After reset, the
  terminal profile reports 90 degrees, Writer is active on display0, the USB
  shell remains available, and FT6336 initializes successfully. See
  `.research/hardware/landscape-upload.log` and `landscape-boot.log`.
  Firmware binary SHA256:
  `2255d7d68a8a6183fc3eeb035fcfd2adc792cda24864819c8276a3f9d8fec62c`.
- Existing input host tests, including 0/90/180/270-degree pointer transforms,
  passed. Physical-input monitoring has not captured button/touch events yet;
  registration success is not claimed as physical input validation.
- Case-corrected build uses 270 degrees: USB-C is on the left short side near
  its upper end when viewed from the front in landscape. Board metadata,
  manifest validation and input orientation host tests passed again.
  Current firmware binary SHA256:
  `6482e8b7bfd5461dc98ea64127c9a98fb1d514ed9689e83fdb7b5504567b2661`.
  Artifact metadata: `build/papermono-writerdeck/build-info.json`.
  Flash hash verification passed; reboot reported `orientation: 270`, Writer
  active on display0, and touch0 ready. Logs: `.research/hardware/case-upload.log`
  and `case-boot.log`. Physical alignment still requires the user's observation.

## Remaining acceptance checks

1. Hold the device horizontally, USB-C on the left short edge near its upper end. Confirm landscape
   Writer text is upright. Opposite orientation is available with
   `setterm orientation 90` (or transient `setterm --display display0 orientation 90`).
2. Test KEY1 through power/pairing status and logs; it is not a generic input
   source. Test KEY2 with `input test buttons` and Enter behavior. Follow the
   [input checklist](m5paper-mono-input-acceptance.md). Full updates take ~4.5 s.
3. Tap inside each corner with several seconds between taps. Query
   `input test touch0` after each. Diagnostic raw coordinates remain in the
   portrait source frame; the dispatcher rotates them for GUI applications.
   At 270 degrees, logical top-left/top-right/bottom-left/bottom-right correspond
   to raw approximately (0,799)/(0,0)/(479,799)/(479,0), allowing for edge margins.
4. Use a BLE keyboard for actual Writer typing, after re-pairing if necessary.
   Writer has no on-screen keyboard and does not consume pointer taps as text.
   A compiled `launcher` session can check pointer selection of displayed icons.
5. Follow the [refresh acceptance record](m5paper-mono-refresh.md) for fast
   updates. Optional peripherals and further sleep/wake coverage remain separate
   acceptance tasks; none are inferred from successful boot/full refresh.

## Optional USB diagnostic shell bootstrap

Display boards do not automatically start a USB shell. This session populated
`/.shell/startup` with the following standard SolarOS command:

```text
session create shell cdc0 --term dumb --size 80x25
```

For a fresh port without a paired keyboard, the following creates that filesystem
image using the installed ESP-IDF utility. **Writing it replaces the entire
internal storage partition**, including any documents. Back up first; do not
repeat on a device containing work. It is separate from ordinary firmware upload.
The address/size below apply to this repository's 16 MB `partitions.csv` only.

```powershell
New-Item -ItemType Directory -Force build/papermono-storage/.shell | Out-Null
Set-Content -Encoding Ascii build/papermono-storage/.shell/startup 'session create shell cdc0 --term dumb --size 80x25'
$idfPython = "$PWD/.pio-core/penv/Scripts/python.exe"
& $idfPython .pio-core/packages/framework-espidf/components/fatfs/wl_fatfsgen.py build/papermono-storage --partition_size 0x1F0000 --sector_size 4096 --fat_count 1 --long_name_support --output_file build/papermono-storage.bin
& $idfPython -m esptool --chip esp32s3 --port COM39 write-flash 0xe10000 build/papermono-storage.bin
```

## Changed files

- `boards/manifests/m5paper_mono.toml`, `m5paper_mono_bringup.toml`: board profiles.
- `boards/expansion_drivers.toml`, `packages/solar_os_packages.toml`: bindings,
  adapter package and build closure.
- `src/services/solar_os_m5papermono_power.c`: M5PM1/M5IOE1 adapter.
- `src/drivers/epd_ssd1677.c`, `.h`: external reset and OTP panel configuration.
- `src/services/solar_os_ssd1677.c`, `solar_os_ssd1677_driver.c`: GPIO-line bindings.
- `src/drivers/ft6336.c`, `.h`: external reset callback.
- `src/services/solar_os_ft6336.c`, `solar_os_ft6336_driver.c`: touch power/reset.
- `src/drivers/m5paper_mono.LICENSE`: M5Stack attribution.
- `src/apps/solar_os_writer.c`: optional board policy to disable cursor blinking
  on the full-refresh panel; unchanged default for all other boards.
- `tests/host/epd_ssd1677_test.c`, `m5papermono_power_test.c`, `ft6336_test.c`,
  `Makefile`: register, waveform, reset and failure-path checks.
- `doc/manual/boards.md`, this guide: usage, sources and verification record.
- System KEY acceptance follow-up: `tests/test_board_manifests.py`,
  `doc/m5paper-mono-input-acceptance.md`, and shared BLE reconnect handling in
  `src/services/solar_os_ble_hid.c` with `tests/host/ble_hid_test.c`.
- `.gitignore`: local toolchain/research and host-test artifacts.

Build artifacts are saved under `build/papermono-writerdeck/`. Full research,
build and serial logs are in the ignored `.research/` directory. The flash
backup may contain original device settings; keep it local.
