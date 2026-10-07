# PaperMono platform services / low-power integration

## Task validity review (before implementation)

The four services share the existing i2c0 and the early M5PM1/M5IOE1 adapter;
implementing them together is appropriate. They do not require changes to BLE,
SSD1677 waveforms, Writer scheduling, or application board checks.

| Service | Existing generic interface | Required extension / board selection |
| --- | --- | --- |
| Touch | Expansion driver, GPIO lines, pointer input | Optional expansion enable/power callbacks; FT6336 bindings select default OFF and managed suspend on PaperMono only. |
| Battery | Battery provider, Shell, status, scripting | PM1 voltage/source plus brief IP2315 status access; opt out of generic voltage-to-percent fallback. |
| RTC | RTC provider, UTC time service, NTP write-back | Native RX8130CE register driver and expansion adapter. Restore valid UTC into system clock; invalid RTC does not prevent boot. |
| LED | Generic status LED | Optional provider for off-chip control. PM1 red LED; initialize IOE green/blue outputs OFF. |

Disabled touch retains its device/resource registration but has no polling task,
no FT6336 transactions, IRQ input/pulls disabled, reset LOW and PYG13 LOW. The
PaperMono schematic shows a dedicated AW35122 power switch and a level shifter
whose touch-side supply is TP_VDD. Holding reset LOW avoids driving an unpowered
controller HIGH. This is a runtime peripheral shutdown, not the PowerDemo's L2
whole-board shutdown (which resets IOE and releases pins). Enable restores power,
reset, controller presence, then input readiness and worker. Suspend preserves
the requested ON/OFF state; explicit deep sleep restarts from the manifest default.

No new battery polling task is needed: use the existing service cadence with a
30-second hardware cache. IP2315 is disconnected immediately after status reads,
including errors, under one serialized I2C session. No charger current/voltage
register is written. No fuel gauge exists in this integration; SOC, remaining
capacity and battery presence are unsupported. Status-only refresh remains OFF.

PM1 reg 0x06 bit 4 is LED_EN_PP, independent of bits 0..3 controlling charger and
rails. Red LED OFF uses a masked update. IOE PWM1/2 enable bits are reg 0x1c/0x1e
bit 7; green/blue are PYG8/PYG9. No animation or pairing indicator is introduced.

RX8130CE has no chip identity register: address/protocol and calendar/control
validation establish presence, not a fabricated silicon ID. Read calendar in one
transaction, validate BCD and Gregorian dates, and expose VLF/STOP as invalid
time. Do not clear invalid flags merely on probe. Native UTC calendar remains on
the 3V3_L0 domain shown in the schematic. RTC IRQ routes through PM1 GPIO0; it is
not an ESP GPIO and alarm/wake scheduling is outside this phase.

Other boards keep current defaults: new driver callbacks and touch parameters are
optional; battery percentage fallback remains available unless a provider opts
out; the native GPIO LED backend remains supported. Power savings are expected
from stopping touch and LEDs, but actual current must be measured separately.

## Primary sources

- [M5Stack PaperMono](https://docs.m5stack.com/en/core/PaperMono), pin map and charger I2C mux warning.
- [PaperMono schematic V0.6.2](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1267/PaperMono_SCH_V0.6.2_20260522.pdf), RTC L0 supply and touch power/level-shifter wiring.
- [M5PM1](https://github.com/m5stack/M5PM1), official register map: reg 0x04 power-source bitmap, reg 0x22/23 battery millivolts, reg 0x06 LED/rail bits.
- [M5IOE1](https://github.com/m5stack/M5IOE1), official GPIO and PWM register map.
- [PaperMono PowerDemo](https://github.com/m5stack/M5PaperMono-PowerDemo), touch reset/hibernate sequences; full L2 power policy is not copied.
- [PaperMono UserDemo](https://github.com/m5stack/M5PaperMono-UserDemo), RGB ownership and active HIGH behavior.
- [EPSON RX8130CE application manual ETM50E-10](https://download.epsondevice.com/td/pdf/app/RX8130CE_en.pdf), 2026-01-30, calendar / flags / STOP / INIEN.
- [INJOINIC IP2315 V1.18 manufacturer datasheet](https://datasheet.lcsc.com/datasheet/pdf/349c2e047ece1948236c4ef68a52da9e.pdf?productCode=C2764357), reg 0x01 bit 0 charge enable and 0xc7 bit 6 charge end. The PDF is manufacturer material hosted by LCSC.

## Implementation and automatic verification result

The production firmware was built and app-only flashed to COM39 (ESP32-S3 v0.2,
MAC 28:84:85:44:79:68). Implementation and automatic checks are complete. Human
acceptance remains open: this is **not yet a claim that all phase success criteria
have passed**. USB-free complete power OFF, corner touches, LED observation,
ZMK OFF/ON and Writer/suspend operation still need the user. Current, DMM voltage
accuracy and RTC drift are unmeasured. Earlier phase acceptance is not substituted
for acceptance on this firmware.

### Source revisions

| Source | Inspected revision |
| --- | --- |
| M5PM1 (1.0.7) | `be9a5456c007c333e7ac963f33bfde1ffa5d82ee` |
| M5IOE1 | `846eec7d05e25c09013be2acdb8804487f48a62e` |
| M5GFX | `c5a3fefad0b38a52cc750e2c4761e339a69a7208` |
| PaperMono PowerDemo | `cf4f57c638aa3ad0168bf90beebdaa3599203bb2` |
| PaperMono UserDemo | `c1099107271d31a0678d661a896e2b04dbb331ea` |

Register decisions use the official register maps, manufacturer documents and
schematic linked above. FT6336 hardware handling uses the existing driver,
official PowerDemo power/reset sequence and schematic; no undocumented FT6336
hibernate register write is added. M5GFX is retained as a display integration
reference; its display path is not altered.

### Architecture before and after

Previously, the PM1/IOE adapter exposed GPIO lines; touch was active from boot;
battery had no provider; RTC reads were unavailable; the red LED had no generic
service. The adapter now also provides battery and red status LED services. A
native RX8130CE driver registers a calendar-only generic RTC provider. The
existing FT6336 expansion service implements optional runtime enable and power
callbacks, selected by the PaperMono manifest. Applications call generic services.
No application board-name branch, Sketch modification or BLE change was added.

Other FT6336 boards retain default enabled behavior and unmanaged power unless
their binding opts in. Existing battery providers retain percentage fallback;
the new opt-out is explicit. The GPIO LED backend remains available. All new
expansion callbacks are optional; drivers without them retain current behavior.

### Touch lifecycle and commands

```text
expansion devices
input status
expansion enable touch0
expansion disable touch0
```

Boot bindings are `enabled=0 managed_power=1`. A disabled device stays attached
and retains GPIO/resource leases, while `ready: no` accurately describes its
effective state. Disable marks the source unready, stops/notifies/joins the
worker, purges stale source pointer events and schedules a held-button release,
deinitializes the driver, disables GPIO4 IRQ/pulls, asserts reset LOW, then sets
PYG13 LOW. A per-source priority release slot works even with a full pointer
queue, without discarding another source's events. OFF performs no FT6336 reads
and has no polling worker. Worker retirement timeout returns an error and blocks
restart; power is not removed underneath a live worker.

Enable sets PYG13 HIGH, settles 10 ms, resets LOW for 20 ms, waits 500 ms after
reset HIGH, verifies controller presence, publishes source readiness and starts
the worker. Reset/I2C/task-creation failures run OFF cleanup. This slightly longer
settle time keeps the existing driver's conservative startup delay. The controller
coordinate parser and portrait-to-landscape input transform remain unchanged.

Suspend joins the worker and powers touch OFF. Resume restores the requested
state: OFF stays OFF; ON starts again. Runtime enable during suspend records ON
without powering the peripheral until resume. Generic expansion transition
reservations prevent attach/detach/enable races and roll back failed transitions.
Explicit light sleep participates in the same quiesce/resume path. Deep sleep
reboots into the manifest default OFF. RTC power/reset is never cycled.

Hardware automatic checks completed three ON/OFF cycles in each of two runs
(six cycles total), ending unready/OFF. At boot, IOE DIR03/04 and OUT05/06 read
`b4 15 14 00`: reset PYG6 is output LOW and power PYG13 output LOW. Host tests
verify zero OFF I2C reads and stopped workers; an actual I2C bus analyzer was not
used. Physical four-corner press/release, held-contact disable and orientation
acceptance remain pending.

### Battery: available values and limits

M5PM1 `0x6e` provides raw battery voltage and the source bitmap. IP2315 `0x75`
is behind the IOE PYG11 switch. A bus session holds the I2C lock from mux selection
through cleanup. It always disconnects the charger after an attempted read,
including selection failure. Failure to disconnect fails the sample. No IP2315
register, charge current or charge voltage is written.

| Item | Decode / reporting |
| --- | --- |
| Voltage | `(reg[0x23] << 8) | reg[0x22]`, integer millivolts. Both are 8-bit registers in the PM1 datasheet; the official header's older “high 4 bits” comment is not used to truncate values above 4095 mV. |
| External power | PM1 reg04 bitmap bits0/1, not an enum. Report through `external_power_known`. |
| Charging | With external power and PM1 CHG enabled, IP2315 reg01 bit0 enabled and regC7 bit6 clear. This is a coarse enabled/not-complete state, not a decoded instantaneous charge-current measurement. |
| Charge complete | IP2315 regC7 bit6, meaningful when its charger is enabled. |
| No external power / charger disabled | Known not charging; charge-complete unavailable. |
| IP2315 access failure | Voltage/source remain available; charging unknown and charge-complete unsupported/unknown. |
| SOC, remaining capacity, ETA | Unsupported. No linear voltage-to-percent estimate is shown. |
| Physical battery presence / detailed charger fault state | Unsupported; PM1 voltage-valid/source bits do not prove physical presence. |

Real hardware returned **4.128–4.130 V**, external power present. IP2315 access
returned `ESP_ERR_INVALID_STATE`; after every attempt the mux was disconnected.
Charging therefore reports `unknown`, and completion `unsupported/unknown`.
The error does not establish that the battery is low or that charging has failed;
the cause is not yet established. The official documentation describes modes
where charger I2C cannot be used. USB removal/charging transition checks and DMM
comparison are pending; no offset/calibration was invented.

The adapter caches hardware reads and errors for 30 seconds. Existing generic
voltage averaging remains in place; no new battery task was added. `battery`
shows voltage, external supply and charging validity. `battery monitor` supports
the existing monitor controls. Lua/Python/agent APIs expose validity and completion;
unsupported percentage is omitted/nil/None/null as appropriate. Generic status
receives external-power state but does not render a fabricated SOC indicator.
`SOLAR_OS_BOARD_TERMINAL_STATUS_REFRESH_ON_CHANGE=0` remains unchanged: battery
updates alone do not refresh the E-paper.

### RTC: native driver and time policy

The selected device is RX8130CE at `0x32`, consistent with the schematic and
official sources. It has no identity register, so probe checks the documented
address/register protocol rather than claiming a silicon ID. Calendar `0x10..16`
contains BCD seconds/minutes/hours, one-hot weekday, day/month/year 2000..2099.
The native driver validates BCD, reserved bits, weekday, leap years and dates
through generic SolarOS validation. A serialized bulk read uses the chip's
calendar read latch; no STOP is used for reads.

Flag `0x1d` VLF bit1, control `0x1e` STOP bit6/TEST bit7 and failed local writes
invalidate time. Probe does not clear VLF or fabricate a calendar; invalid RTC
warns and boot continues. INIEN in control1 is enabled by a masked update that
preserves CHGEN and other bits. A successful set performs STOP, bulk calendar
write, restart, then W0C flag write `0xbd` to clear only VLF while preserving
other event flags and keeping reserved bit6 zero. Restart is attempted even if
STOP/calendar writing reports an error. Other PM1/IOE clients cannot interleave
inside that bus session.

The hardware calendar stores **UTC**. Existing timezone settings remain
presentation policy. A valid provider explicitly restores `settimeofday()` at
boot; successful manual setting updates system time too. The existing NTP
service already converts network time to UTC and calls the generic setter,
so it now writes this RTC; no independent NTP client was introduced. Network
NTP operation itself was not exercised in this offline automatic test.

The PC's UTC date/time was set through existing `date` / `time` commands after
confirming the unchanged timezone was UTC. On software reset, boot logged
`rx8130ce: UTC restored from RTC`; the calendar advanced from 17:13:21 to
17:14:46 on 2026-10-07 UTC. A raw read confirmed calendar `48 14 17 08 07 10 26`
and flags/control `2d 00 30` (VLF/STOP/TEST clear). Host tests also verify the
actual RTC adapter, generic time validation and system-clock update path, not
only register conversion.

This proves software-reset restoration with USB present. It **does not replace
the required battery-only complete power-OFF test**. RTC drift is unmeasured.
VDD/VBAT are on L0; suspend does not alter this domain. Future alarm/wake support
must route RTC IRQ through PM1 GPIO0 and define alarm/event clearing and PM1
wake policy. No alarm/wake scheduler or direct ESP GPIO IRQ is claimed.

### LED ownership and preservation

The red LED uses PM1 LED_EN_PP, reg06 bit4. Early initialization masks bits0..2
to the pre-existing enabled charger/rail values and bit4 to OFF, preserving boost
and unrelated bits. Generic `led status|on|off|toggle` subsequently masks bit4
only. No charging parameter is changed and no charger safety behavior is disabled.
Green/blue are IOE PYG8/PYG9, controlled by PWM2/PWM1. Disable their PWM ownership,
preload LOW and select push-pull output using only the corresponding bit masks.
No RGB effect or pairing indication is introduced.

Actual register readback: PM1 reg06 `07` (red OFF; charger/DCDC/LDO still enabled),
IOE PWM registers `0x1c..20 = 00 00 00 00 00`, and RGB output latches LOW.
Generic `led status` reads `off`. Red/green/blue visual confirmation and the
point during cold boot when the LED extinguishes are still pending. Application
OFF policy cannot govern earlier bootloader/hardware illumination.

### Tests, build, data and firmware

| Verification | Result |
| --- | --- |
| Feature host tests (9 binaries) | PASS: PM1/IOE provider/masks/cache/errors; FT6336 low-level and lifecycle; battery; input priority release under queue saturation; expansion reservation/rollback; generic RTC; native RX8130CE plus actual generic time; LED provider. |
| Related regression host tests (9 binaries) | PASS: GPIO controller, terminal preferences/large font, async display immutable/latest snapshots, Writer checkpoint concurrency, FAT replacement/recovery, SSD1677, production BLE reconnect worker, timezone. |
| Board manifest tests | 33/33 PASS, including selected PaperMono services and default OFF binding. |
| Full Python suite | 553 run, 4 skipped, 2 errors and 2 failures. Four existing source-text fixtures in `test_runtime_boundaries.py` expect pre-existing BLE layout/order and an old keyboard delegate statement. These assertions were not rewritten or used to justify BLE changes. |
| Firmware | Normal `writerdeck`, `m5paper_mono`, OTA layout, BLE security trace OFF; ESP-IDF 5.5.5 build PASS. |
| App flash | Only `0x10000`, 3,340,064 bytes; esptool verified the written hash. NVS/FAT not erased or flashed. |
| Automatic device checks | Boot Shell, `version`, `ls`, fastest startup/mode, JP keyboard, font query, touch cycles/readiness, LED, battery, RTC set/read and reset restoration PASS. |
| BLE | Existing travelers-split automatically connected and input source reported ready after reset. Bond unchanged. Physical ZMK OFF→ON/input regression still pending. |
| Display / Writer | 45 protected BLE/display/Writer source files hash-identical to this phase's starting snapshot. Fastest/40 MHz/tile/LUT/async paths unchanged; existing Writer data/settings retained. Physical editing/Alt+Tab/suspend regression pending. |
| Data | All 38 saved FAT files compared; only `/.shell/HISTORY` changed. Documents, checkpoints, startup and saved config byte-identical. |
| NVS | All logical settings unchanged except the required monotonic WireGuard boot reservation (+4). Remembered peer and all 5 NimBLE bond-store keys byte-identical; no reset/re-pair. |

The four full-suite failures are:

- `test_ble_hid_replaces_a_conflicting_keyboard_bond`
- `test_ble_reconnect_is_scan_gated_to_the_remembered_peer`
- `test_ble_reconnect_preserves_matched_candidate_until_open`
- `test_script_and_ble_policies_are_delegated`

| Build metric | Previous normal firmware | Current normal firmware | Delta |
| --- | ---: | ---: | ---: |
| Used app flash | 3,322,931 B | 3,334,283 B | +11,352 B |
| Reported static RAM | 52,480 B | 52,904 B | +424 B |
| App binary length | 3,328,704 B | 3,340,064 B | +11,360 B |

Static RAM is PlatformIO's reported on-chip metric, not a measured runtime heap
or total PSRAM usage. The input release slots are placed in external BSS; no
current or complete memory-use figure is inferred from the build delta.

Artifacts:

- `build/papermono-platform/firmware.bin` — SHA256 `30edf4efea54c9a5eadfbf8e7e6d6b564edce231ff3c80d37d8953b61aeef952`
- `build/papermono-platform/firmware.elf` — SHA256 `2ff8bf03b809fb189da4adf071e2f26f42a209569c326b5598bc0026228ad736`

Local evidence is under `.research/platform/`: `before/` source hashes and device
backups, `flash.log`, `auto-checks.log`, `reboot-checks.log`, `build-final2.log`,
`python-tests.log`, `nvs-compare.log` and `fat-compare.log`. Sensitive bond contents
are not printed in the report. Existing unrelated workspace changes were retained.

### Changed files in this phase

| Area | Files |
| --- | --- |
| Selection | `boards/manifests/m5paper_mono.toml`, `boards/expansion_drivers.toml`, new `boards/drivers/rtc_rx8130ce.cmake`, `packages/solar_os_packages.toml` |
| RTC | New `src/drivers/rtc_rx8130ce.c/.h`, new `src/services/solar_os_rx8130ce.c`, `src/services/solar_os_time.c/.h` |
| Power/battery/LED | `src/services/solar_os_m5papermono_power.c`, `solar_os_battery.c/.h`, `solar_os_status_led.c/.h` |
| Lifecycle / bus / input | `src/main.c`, `src/services/solar_os_expansion.c/.h`, `solar_os_buses.c/.h`, `solar_os_input.c/.h`, `solar_os_ft6336.c/.h`, `solar_os_ft6336_driver.c`, `src/drivers/ft6336.c/.h` |
| Generic consumers / commands | `src/shell/solar_os_shell_expansion.c`, `solar_os_shell_hardware.c`, `src/apps/solar_os_lua.c`, `solar_os_python.c`, `src/services/solar_os_agent_tools.c` |
| Tests | `tests/host/Makefile`; new `ft6336_lifecycle_test.c`, `rtc_rx8130ce_test.c`, `status_led_provider_test.c`; updated `m5papermono_power_test.c`, `input_test.c`, `battery_service_test.c`, `expansion_registry_test.c`; bus/GPIO/SNTP/task stubs; `tests/test_board_manifests.py` |
| Report | `doc/m5paper-mono-platform.md` |

### Remaining hardware acceptance and next phase

1. Confirm all LEDs visually OFF. Confirm no touch counters change while OFF;
   enable touch and perform four-corner press/release in landscape (USB-C upper
   left side). Repeat disable/enable at least three times with real contacts,
   including disabling while held; ensure no stuck press. Finish touch OFF.
2. Compare `battery` under USB and battery-only operation, with charging if
   available. If a DMM is available, record external voltage and offset; do not
   add calibration without evidence. Charging state access remains unresolved.
3. Check correct RTC date/time, unplug USB, completely OFF via red power button,
   wait several minutes, then boot on battery only. Record elapsed time and RTC
   restoration. KEY1 suspend is not complete power OFF.
4. Check remembered ZMK already-ON boot and one OFF/ON reconnect without bond
   changes. Run Shell `version` / `ls`, open an existing Writer document, edit
   and save, Alt+Tab, then KEY1 suspend/resume. Verify touch requested OFF/ON
   survives the same transition and the existing fastest/manual-cleanup behavior.
5. If suitable current-measuring hardware is available, compare LED ON versus
   OFF and touch ON versus OFF with battery power, Shell idle, BLE connected,
   display idle and no USB. Record duration/min/median/max; measure reconnect and
   refresh separately. **Actual current not measured**; no before/after saving
   or battery-life claim is made. Functional OFF registers/runtime are verified.

Future Sketch integration can call the generic device enable/disable API; before
automatic integration, define per-application acquire/release ownership, preserve
the user's prior requested state, release on all app exits/errors, and reconcile
with suspend and concurrent users. A reference-counted touch lease is not part
of this phase. Sketch drawing behavior remains unchanged.

The sections above cover the requested 40 reporting items: review/sources and
before/after (1–3), file inventory (4), touch (5–11), battery (12–18), RTC
(19–24), LED/power (25–30), regressions/tests (31–34), build/data/artifacts
(35–38), and open acceptance/Sketch prerequisites (39–40).
