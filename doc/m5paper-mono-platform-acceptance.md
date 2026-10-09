# PaperMono platform services final acceptance

## Final result (2026-10-10 JST)

**Required Platform Services acceptance: PASS.** The final uninstrumented
release passes the 200-round/601.7-second shared-bus check, four-minute complete
hardware-OFF RTC retention, and both battery-only KEY1 suspend/resume conditions
(touch requested ON and OFF), with explicit human confirmation of display and
contact behavior. Final read-only FAT/NVS comparison passes; touch finishes OFF.
Earlier failed candidates below are diagnostic history, not results of this
accepted artifact. Optional DMM calibration/current consumption remain
**NOT MEASURED**; no battery-life improvement or long-term RTC drift is claimed.

Final artifact: `build/papermono-platform-safe-telemetry/firmware.bin`,
3,334,400 bytes, SHA256
`3bfc6e112e80445ef34d1958217049a222e8d387180d4c3e43f0db0ea8ac33db`.
Application-only flashing at `0x10000` covers `[0x10000, 0x33e100)`;
NVS/FAT are not erased or written by flashing. Final tests pass: 129 host
executables and 565 Python tests, with four skips.

To protect the shared bus, automatic telemetry keeps the optional IP2315
charger branch disconnected. Actual PM1 voltage, estimated percentage and
power source remain available; USB charging/completion is explicitly unknown.
The electrical failure mechanism is not claimed as proven. BLE/Writer physical
regressions were confirmed earlier in this acceptance; extra keyboard-dependent
repeats are skipped at the user's request to leave ZMK OFF. Their production
policy/rendering remains unchanged apart from the observed reserved Alt-marker
input guard. Next phases (Sketch integration and display quality work) remain
out of this task.

## Review before implementation (2026-10-09)

Baseline: `86d386a1`, branch `experiment/papermono-text-contrast`. The only
pre-existing untracked directory is the user's `.vscode/`; it is excluded.

The remaining acceptance scope matches the current manifest, M5PM1/M5IOE1
provider, battery service/status-bar integration, FT6336 lifecycle, RX8130CE
time service, generic status LED provider, and expansion suspend transitions.
The earlier platform report predates the accepted `refresh=typing` experiment.
That display implementation and the device's actual startup/preferences must
remain intact. No display, BLE, Writer, keyboard-layout, or partition changes
are required by this task.

The minimum feature change is an optional battery-provider voltage estimator,
called after the existing eight-sample voltage average. PaperMono supplies the
official 3300–4200 mV mapping instead of opting out of percentage estimation.
Fuel-gauge readings retain precedence, explicit percentage-unavailable retains
precedence, and providers without the callback retain their existing behavior.
The result uses the existing `percent_estimated` API/CLI metadata. Generic UI
already separates plug indication from proportional battery fill; no board-name
condition belongs there. The 30-second hardware cache and single averaging
stage stay unchanged. Charging validity remains independent of voltage/source.
There is no architectural blocker requiring a broader redesign.

This review describes the pre-edit scope. Later binary acceptance discovered
a reserved Alt key marker entering Writer text; its minimal input guard is
documented below as a directly observed regression cleanup.

Software/register lifecycle, fault rollback, time/calendar validation, LED bit
preservation, battery mapping, rendered icons, and status-only refresh suppression
can be tested automatically. Live persistent-state retention needs before/after
device evidence. Physical corner orientation, held-touch behavior, cold-boot LED
appearance, battery-only icons, complete-power-off RTC retention, BLE/Writer
flows, and display appearance require explicit human confirmation. Voltage
calibration/current consumption need suitable external equipment.

## Official battery source

Independently verified `refs/heads/main` with `git ls-remote` on 2026-10-09:
`c1099107271d31a0678d661a896e2b04dbb331ea`.
The inspected reference files are unchanged in the local research clone.

- [Official battery application](https://github.com/m5stack/M5PaperMono-UserDemo/blob/c1099107271d31a0678d661a896e2b04dbb331ea/main/apps/app_battery/app_battery.cpp)
- [Official voltage HAL](https://github.com/m5stack/M5PaperMono-UserDemo/blob/c1099107271d31a0678d661a896e2b04dbb331ea/main/hal/hal.cpp)

Use `clamp((averaged_mv - 3300) * 100 / 900, 0, 100)`, integer floor
inside the range. This is a voltage estimate, not fuel-gauge SOC. Do not copy
the reference UI filter into SolarOS or infer capacity/remaining runtime.

## Acceptance record

The latest uninstrumented candidate keeps the optional charger branch
disconnected after controlled USB-bus comparisons. Its four-minute complete
OFF retention, cold Shell display and LED shutdown are confirmed. The final
600-second shared-bus run passes 200 rounds, and normal-release battery-only
touch ON and OFF suspend/resume both pass. The final persistent-data audit also
passes. Earlier failures and rejected candidates remain recorded below.
The reserved-key input guard, real saved z4 and intended text edits have
separate human/binary evidence. Keyboard-dependent repeats are deferred at the
human's request (ZMK OFF); no BLE policy changes are made in this task.

| Area | Automatic evidence | Human evidence | Verdict |
|---|---|---|---|
| Battery voltage | Final USB 3.842 V; final ON-resume cached battery sample 3.832 V; PM1 read/averaging tests pass | Earlier battery-only 3.750 V; DMM not measured | PASS |
| Battery estimated % | Final USB 60% estimated; final ON-resume cached battery sample 59%; clamp/floor/averaged-input/priority tests pass | Earlier battery-only 50%, Power battery, Charging no | PASS |
| External power/status icon | USB external; icon and status-only suppression tests pass | USB plug and battery-only ordinary icon explicitly confirmed; tiny inner details not resolved visually | PASS |
| Charging state | USB charging explicitly unknown; battery-only noncharging; host tests prove no charger access/mux writes and retain real voltage/source/error propagation | No charger-state claim | PASS |
| Touch OFF | OFF contacts produced zero events; source attached/not ready; worker-stop log, reset/power OFF | Human touched screen during OFF | PASS |
| Touch ON/corners | Ordered contacts map to four landscape quadrants; cycles and idempotence verified | Human confirmed contacts and three repeated cycles | PASS |
| Held-touch disable | Held press becomes release/buttons=0 after disable; no stuck state | Human confirmed disable while held, then release | PASS |
| Touch suspend/resume | Host lifecycle tests pass; final normal ON round has ready=yes/real press-release; OFF round stays not-ready with unchanged event count; both shared-bus readbacks valid | Final battery-only ON and OFF display resume/contact behavior confirmed | PASS |
| RTC software operation | Host tests and standard setter pass; final normal 200-round/601.7-second soak validates calendar/flags/PM/IOE with no retries/resets | Date/time confirmed; final cold-boot system time matches wall clock | PASS |
| RTC complete-power-off retention | Final normal-release UTC calendar/flags valid after complete OFF; readback matches PC interval to one-second resolution; no time setter | Human confirms final 23:51 OFF / 23:55 battery-only ON, Shell visible and LEDs OFF | PASS |
| Red LED | Live PM1 0x06=0x07 (bit4 OFF), generic provider OFF; masked-write tests pass | Human confirms LED OFF after USB-free cold boot | PASS |
| Green/blue LEDs | Live IOE PWM enable/output bits OFF; preservation tests pass | Human confirms LEDs OFF after USB-free cold boot | PASS |
| BLE boot reconnect | Remembered connection restored; reconnect/HID tests pass | Human confirms ZMK version after USB-free cold boot with ZMK already ON | PASS |
| BLE OFF→ON reconnect | Connected remembered travelers-split after round; production/security patch unchanged | Human confirms automatic reconnect and version after real ZMK OFF→ON | PASS |
| Writer | Rendering/storage/checkpoint tests pass; target Alt-marker injection and real Alt+Tab/save append two X bytes, with no additional reserved marker; saved checkpoint checksum valid and zoom=4 | Load/switch/resume/edit/save/reopen/z4 confirmed; post-guard z4/end X, Alt+Tab/version/back, save and exit explicitly confirmed | PASS |
| User data preserved | Final 40-file audit differs only in intentional async edit/checkpoint/history; startup/settings/5 bonds/peer unchanged; documented SDK PHY calibration and monotonic boot-reservation exceptions audited separately | Existing document reopen/settings confirmed; whole-text deletion was intentional | PASS |
| Display regression | Host tests pass; transport/waveform unchanged; final normal ON/OFF suspend/resume and shared-bus soak pass | Cleanup/cold Shell and final ON/OFF resume confirmed | PASS |
| Current consumption | No equipment confirmed | No measurement | NOT MEASURED |

## Implementation and validation

Production files changed:

- `src/services/solar_os_battery.h`: optional provider estimation callback.
- `src/services/solar_os_battery.c`: invoke callback on the existing averaged
  voltage only when no real provider percentage is available; clamp its result.
- `src/services/solar_os_m5papermono_power.c`: official voltage mapping and
  callback registration; remove the percentage opt-out. Final automatic battery
  reads use PM1 voltage/source without selecting or accessing the optional
  charger branch. External charging is explicitly unknown; battery-only charging
  is known false. The cache, averaging, LEDs and GPIO ownership remain intact.
- `src/apps/solar_os_writer.c`: ignore `SOLAR_OS_KEY_ALT_PREFIX` rather than
  inserting its reserved 0x93 byte as text. Rendering/scheduling is unchanged.
- `boards/manifests/m5paper_mono.toml` and `src/drivers/i2c_bus.c`: native
  PaperMono I2C GPIO sleep retention with bus ownership and rollback semantics.
- `scripts/papermono_sdkconfig.cmake` and `scripts/papermono_sleep.defaults`:
  reproducible board-only sleep/USB settings, preserving unrelated local values.
- `scripts/patch_i2c_rx_completion.py`, `patches/i2c/overlay.cmake`, root
  `CMakeLists.txt` and `scripts/platformio_solaros_flavor.py`: build-local
  official I2C completion backport and configuration integration.

Test files changed: `tests/host/battery_service_test.c`,
`m5papermono_power_test.c`, `terminal_render_test.c`,
`ft6336_lifecycle_test.c`, plus `tests/test_i2c_rx_completion.py`,
`test_i2c_sleep_pins.py` and `test_papermono_sdkconfig.py`. Four stale source-policy
assertions in `tests/test_runtime_boundaries.py` are updated to the current
architecture without changing BLE production behavior. Final suites pass:
129 host executables and 565 Python tests (4 skips).
This report is the only documentation addition.
Local evidence/helpers are in ignored `.research/platform-acceptance/`.
The user's untracked `.vscode/` is untouched.

Focused tests pass: service estimator gets 3675 mV after a 3600→4200 mV step,
not the raw 4200; fuel-gauge/unavailable precedence and clamping; official
mapping endpoints/interior integer floor; charger failure independent validity;
icons distinguish 0/50/100%, external plug independent of fill, unknown slash;
120 status-only changes cannot present a PaperMono frame. Touch tests cover
three held-contact lifecycle cycles, failed start/join rollback, ON/OFF suspend
restoration, and enabling during suspension without power/reads/worker startup.

Normal broad host suite: **PASS**, 129 test executable invocations, using WSL
and explicit `CJSON_DIR=../../.pio-core/packages/framework-espidf/components/json/cJSON`.
The initial invocation without this local SDK override failed to find cJSON;
no production change was made for that environment issue.
The extended deferred-touch test was rerun successfully after the broad suite.
After the Alt-marker guard, focused keys/input/Writer buffer/checkpoint tests
and the full host suite pass again (`host-final.log`). The Python suite was also
rerun: 554 tests, the same two failures/two errors/four skips (`python-final.log`).
The board firmware build passes. Reserved-key injection on the actual Writer
and final saved-file byte comparison provide the guard's runtime verification.
After actual Alt+Tab/version/back and human Ctrl+S/exit, the file is exactly
the preceding 10-byte document plus two probe X bytes. The prior 0x93 marker
count remains one; the guard prevents new markers and does not rewrite old
user content. Saved checkpoint checksum is valid and zoom remains 4.

Python suite (WSL): **554 tests, 2 failures, 2 errors, 4 skipped**. All four
failures are the unchanged source-text fixtures below, also recorded before
production edits in the 42-test `test_runtime_boundaries` baseline:

- `test_ble_hid_replaces_a_conflicting_keyboard_bond`
- `test_ble_reconnect_is_scan_gated_to_the_remembered_peer`
- `test_ble_reconnect_preserves_matched_candidate_until_open`
- `test_script_and_ble_policies_are_delegated`

The referenced BLE/script production and fixture files are unchanged. The full
Python suite is **not green**. A Windows discovery attempt also exposed existing
Python-version/encoding/compiler environment incompatibilities; WSL is the
usable broader test environment. Board manifest/package generation tests pass
there. `git diff --check` passes.

## Firmware and persistent-data evidence

Normal (security trace OFF) `m5paper_mono` / `writerdeck` / `ota` build passed.

- Artifact: `build/papermono-platform-acceptance/firmware.bin`, 3,343,040 bytes.
- SHA256: `2fdc8c30741f5b14806d93cb5af5ed0e32cdcea0fe4a747060d94e36e083655e`.
- RAM: 52,904 bytes; flash program usage: 3,337,267 bytes.
- Current USB detected as COM39, Espressif VID:PID 303A:1001, matching the known
  PaperMono USB serial. Before firmware reports SolarOS 4.16.1 / writerdeck.
- esptool application-only write at `0x10000`, end exclusive `0x3402c0`, verified
  written data hash. No factory/bootloader/partition/FAT/NVS image was written.
- Backup ranges read from the generated partition table: NVS `0x9000/0x4000`,
  FAT `0xe10000/0x1f0000`. Snapshots are kept locally, never dumped as secrets.
- Before/after logical FAT comparison: **40 files, zero changed or missing**.
- `/.shell/startup` SHA256:
  `a7696b40328a32964dd022087ac8a1acb2b515ed6a6c3fe8f7ab736fb2391c60`.
- `/.shell/startup.before-fastest` SHA256:
  `5640ee5aa2d80e960ccd684587f2e51de3919271523442b02b861ee8b049c08e`.
- NVS: **38 logical keys**, 5 NimBLE bond keys and remembered `blekbd/peers`
  byte-identical. The only changed value is the existing monotonic WireGuard
  boot time reservation (`tai_ceiling`, advanced by two reservations); it is
  intentionally not rolled back. User preferences/layout/timezone are unchanged.
- Before/after runtime: Shell mono 20 px, JP layout, UTC+9, startup explicitly
  requests `refresh=fastest`. The task does not overwrite this user startup
  preference or change the generic safe boot policy.

USB runtime battery: **3.758 V, 50% estimated, external, charging unknown,
charge complete unsupported/unknown, capacity unset**. The existing CLI suffix
`uncalibrated ADC` represents its generic calibration flag; the actual voltage
source is the PM1 millivolt registers, not a newly added ADC measurement.
The existing CLI Min/Max settings (3000/4120 mV) remain the generic fallback
configuration; PaperMono's provider estimate uses 3300/4200 mV and does not
rewrite NVS fallback settings. No DMM calibration has been performed.

RTC live UTC calendar and UTC+9 system display agree; VLF/STOP are clear
(`0x1d..0x1f = 29 00 30`). This proves software restoration across USB-assisted
reboots, not the required several-minute complete-power-off retention.

Final binary comparison before the Alt-marker guard (`final-comparison.log`):
40 FAT files, no missing/new files. Exactly three intentional differences:
`async-test.md` (human intentionally deleted original text; 190→10 bytes), its
`/.writer/09322538.recovery` checkpoint, and Shell history. The saved checkpoint
has zoom=4, valid checksum and correct path. Both startup script hashes are
unchanged. All 38 NVS keys are retained, with 5 bond keys and remembered peer
byte-identical; only WireGuard's monotonic ceiling changed (eight reservations).
Final NVS SHA256: `ccea0d9344b2b44ee591b15661b655a884193ec49be7722ec2fccb633ed70413`.
Final FAT SHA256: `76d4b4a2d26e739fa53f09f31ddf41228e47bd7dbcbb22c0103d5e8ba55127a0`.
The earlier final-read attempt while USB was disconnected did not touch device
flash; after reconnection read-only NVS/FAT backup succeeded. These reads use
ROM mode and reboot, with all documents saved and no active Writer beforehand.

Latest normal artifact including the Alt-marker guard:
`build/papermono-platform-acceptance-final/firmware.bin`, 3,343,152 bytes,
SHA256 `e0f186471357d8817ce9a03dfda93c0ff1d01cd56f2451cc36f0605ff9819f4c`.
RAM 52,904 bytes; program usage 3,337,371 bytes. Application-only write at
0x10000, end exclusive 0x340330, verified (`alt-marker-flash.log`). This is the
  current installed normal firmware; earlier normal/diagnostic artifacts are
retained separately. No partition, FAT, NVS, startup or bond image was written.

## Remaining physical acceptance

**Current blocker:** during the contact round the live log records KEY short
press suspend at approximately 457.6 s uptime, followed by I2C timeout and
display/peripheral resume failures at 472–473 s. The human explicitly reported
steps 1–4 completed and step 5 not performed; do not reinterpret this as an
intentional successful suspend test. I2C remains registered/shared/ready with
three leases, while IOE, PM1 and RTC transfers fail. Display async reports
`suspended=1 error=259`. BLE input remains connected. The root cause is not yet
established, and no reset/retry workaround is being added to production.

Human round 1 explicitly confirms USB plug/% display, OFF-screen contacts,
ordered corners, disabling while held, and three repeated enable/contact/
disable cycles. First landscape corner samples are approximately (98,105),
(703,103), (104,406), (700,388), matching the existing 270-degree dispatch
transform from native 480×800 to landscape 800×480. Held disable records
release/buttons=0. These narrow contact checks pass; suspend acceptance does
not. Diagnostic logs/summary are saved in `.research/platform-acceptance/`.

A temporary diagnostic build added read-only first-failure GPIO/controller
logging to `i2c_bus.c` (no behavior changes). Its binary is
`build/papermono-platform-diagnostic/firmware.bin`, 3,345,104 bytes, SHA256
`fade489a67aef639f3cbfc7a9e406e961b39f7adf533327da9e22f108d57c222`.
Application-only flashing/hash verification succeeded. This diagnostic firmware
initially failed to attach the IOE/display/touch/RTC while the USB Shell
remained usable. First startup diagnostics were not captured before the USB
collector opened. The temporary source instrumentation has been removed from
the worktree; the diagnostic artifact is preserved for investigation.

Automatic approval review initially rejected a proposed single diagnostic USB
`reboot`, citing conflict with the earlier investigate-without-resetting
instruction. The human then explicitly authorized that diagnostic reboot. It
was executed once with early USB capture; no FAT/NVS/bond erase was performed.
The resulting first-failure diagnostic at 1.641 s shows PM1 address 0x6e/reg0
error 259; SDA=0, SCL=0; controller CTR=0x13, status=0xc000, raw interrupt=0x2.
GPIO47/48 both have input/pull-up enabled, pull-down disabled, open drain and
correct I2C matrix signals; peripheral I2C clock is enabled and reset deasserted.
Software reboot did not clear the failure. This narrows investigation toward
line/retained-GPIO/peripheral state; **it does not establish a root cause**.
The controlled USB-free complete OFF→ON comparison restored the display and
all shared-I2C devices. The human explicitly confirms a fresh Shell, ZMK
`version`, battery/date/time output, and all LEDs OFF. Subsequent readback shows
IOE outputs 14 00, RTC flags 29 00 30, and USB voltage 3.766 V / 51% estimated.
This is cold-start evidence, not a fix for the earlier failure and not the
three-minute RTC retention test.

The next controlled diagnostic round tested requested touch OFF and ON once
each. Both resumes succeeded, as explicitly confirmed by the human. The ON
round restores FT6336 readiness and records a real press/release; final OFF
has ready=no/buttons=0. Post-round I2C/RTC reads and display attachment are
normal, with USB battery 3.770 V / 52% estimated. Evidence is in
`suspend-isolation.log` and `post-isolation.log`. The existing ESP-IDF message
about failing to enable CPU power-down during light sleep appears in these
successful transitions too; it does not by itself explain the bus failure.
No physical-current saving claim follows from configuring automatic sleep.

At 14:42 JST the original normal acceptance artifact was restored with an
application-only write at 0x10000, end exclusive 0x3402c0; hash verification
passed (`normal-restore-flash.log`). **The device now runs the normal firmware,
not diagnostic instrumentation.** Readback confirms all peripherals attach,
touch OFF, startup refresh=fastest, UTC+9 and the original `/transport-test.md`
contents. BLE completes discovery using the remembered peer without bond changes.
The earlier stuck-low failure remains unresolved; the successful isolated
round is recorded without converting that failure into an overall PASS.

Human USB-free round: after the requested 30-second cache expiry, voltage and
estimated percentage were reported OK and the ordinary battery icon confirmed.
The small icon's interior was not resolved by eye; proportional fill/no
fuel-gauge-absence slash is covered separately by host rendering tests. Actual
battery-only voltage/percentage/Power/Charging text was not supplied, so it is
not fabricated from later USB measurements. Cleanup was explicitly confirmed
OK. The human then confirms correct date/time immediately after battery-only
boot following the requested three-minute red-button complete OFF sequence,
and successful ZMK OFF/20-second wait/ON automatic reconnect with `version`.
Exact OFF/ON timestamps and subsecond error were not recorded; this is a short
qualitative retention acceptance, not a drift specification. No NTP/manual
time-set operation was issued during the test.

`retention-readback.log` records normal ready IOE/display/RTC, touch OFF, BLE
connected remembered 1/1, UTC+9, system 2026-10-09 14:52:38 followed by RTC UTC
05:52:40, valid flags 29 00 30, PM1 LED/rails 07, IOE outputs 14 00, and all
expected visible documents. USB telemetry is 3.764 V / 51% estimated,
Power external, Charging unknown. USB reconnect itself is human-confirmed OK.

Human Writer round explicitly confirms original document/load, session switching,
suspend/resume and input, save/reopen, and no new visible corruption. The human
also intentionally changed z1 to z4. Objective readback found `/async-test.md`
changed from 190 to 10 bytes containing the test words, so a discrepancy was
reported before acceptance. The human then clarified that the original text
was deliberately deleted before the test. This is an authorized intentional
edit, not evidence of spontaneous loss, and the baseline is not restored over
the user's choice. The original 190-byte baseline remains locally backed up.
The specific original-text append/retention test therefore was not performed.
The pre-/post-flash 40-file equality remains the separate data-retention proof.

Old `.writer/09322538.meta` remains z1; current checkpoint `.recovery` is the
authoritative source for saved zoom in this architecture, so the old text
metadata alone is not evidence of a zoom failure. Physical saved-z4 retention
is explicitly confirmed and binary comparison validates zoom=4. The read-only Lua API attempt on CDC did not yield
usable output and was stopped with Ctrl+C; CDC is back in Shell, with no reset
or filesystem write from that attempt. Both final and post-guard binary comparisons
pass the protected-file/NVS/bond checks. Readback has ready IOE/display/RTC, touch OFF, BLE connected, and display
async enabled/busy=0/pending=0/suspended=0/error=0, 24 submissions and completions.
The runtime ring also records a transient Writer submission error 259 before
the later successful KEY suspend/resume; the final async state is normal.
That log entry is retained rather than calling the display round error-free.

Hardware/source inspection also confirms 2.2 kOhm external SYS_SDA/SCL pull-ups
to 3V3_L2 (R36/R37), and an AW39112DNR touch-side level translator with VCCA
on switched TP_VDD and VCCB on 3V3_L2. The
[manufacturer's product information](https://www.awinic.com/en/productDetail/AW39112DNR)
specifies that either supply may ramp first. Neither the schematic nor that
specification establishes why both lines remained LOW in the failed run.
The installed ESP-IDF I2C driver already takes its no-light-sleep PM lock around
transactions; simply adding a presumed missing lock would not be an evidenced
fix. No speculative bus clear, repeated power writes, or retry policy is added.

Follow-up diagnostic artifact `build/papermono-suspend-probe/firmware.bin`
(3,346,000 bytes, SHA-256
`5819d56161dd6fa2ca6aea5761843aa7f1bde4b94cd9923a8d28cbdd321f50ac`)
builds a temporary six-cycle harness using the normal runtime suspend/resume
entry points, alternating requested touch OFF/ON and crossing the 30-second
power cache with a 45-second stop. It records the first shared-bus fault and
stops on failure; it neither resets nor retries nor changes persistent settings.
The temporary main/I2C source changes were restored after artifact creation.
It was flashed application-only at 0x10000, end exclusive 0x340e50,
with verified hash (`suspend-probe-flash.log`). The human long-stop round on
the normal firmware explicitly confirms display resume, contact, and `version`.
Six diagnostic cycles finish with PM/IOE/RTC reads successful and touch OFF.
However, at uptime 374085 ms, after cycle five resumed, a background RTC
calendar read (0x32/0x10) returns 259 (`ESP_ERR_INVALID_STATE`, not the distinct
`ESP_ERR_TIMEOUT` 263). SDA/SCL are both HIGH; later reads succeed without a
manual reset. This differs from the earlier persistent both-lines-LOW failure
and is retained as an additional failed observation, not a six-cycle clean PASS.
The temporary first harness did not stop on a background fault; only its own
checkpoint reads controlled progression. The second harness corrects that
limitation and records actual SDK light-sleep calls, success/error counters,
and PM locks without changing SDK source or persistent policy.

Second diagnostic artifact `build/papermono-suspend-sleep-probe/firmware.bin`
is 3,344,912 bytes, SHA-256
`9939e8514ff98a26f7f9925cf54512fc0840d463bea19a67e80878273296c57d`.
Application-only flash at 0x10000, end exclusive 0x340a10, hash verified
(`suspend-sleep-probe-flash.log`). Its temporary main/I2C/linker changes
were restored in the normal source tree after build. The SDK wrapper counts
existing `esp_light_sleep_start` calls and returns their unchanged result.
No extra sleep, recovery, retry, delay, NVS writes or document edits are added.

The second probe reports 181,539 existing sleep calls after six cycles,
**zero successful entries**, all returning 258 (`ESP_ERR_SLEEP_TOO_SHORT_SLEEP_DURATION`).
With the normal non-IRAM sleep path, ESP32-S3 estimates 8,628 us exit overhead;
the runtime polls at 10 ms, so a visually successful suspend does not establish
real automatic light sleep. This is separate from the original persistent bus fault.

Third diagnostic artifact `build/papermono-sleep-config-probe/firmware.bin`
is 3,339,152 bytes, SHA-256
`a0992c326ee328447a5f2a7ffe04a9b260028929bf68b8e59666349f2f7714a0`.
It temporarily enables PM_SLP_IRAM_OPT and PM_RTOS_IDLE_OPT and disables
PM_POWER_DOWN_CPU_IN_LIGHT_SLEEP; normal generated configuration was restored
after building. App-only flash/hash verified. At uptime 37,296 ms, **before
any suspend**, RTC calendar read returns 259 with both bus lines HIGH. The
harness halts before its first transition. Later PM/IOE/RTC reads succeed
without reset (`sleep-config-probe-state.log`). These experimental settings
are not yet a production change or a fix for that RTC error.

Fourth diagnostic artifact `build/papermono-i2c-event-probe/firmware.bin`
is 3,338,928 bytes, SHA-256
`fc0bbf9a9a0ac0def7ab3c809b7fa1aefdac2e404745437cd1bf06ded096832b`.
It adds read-only SDK status/event trace; all normal source/config files were
restored after build. The first flash was interrupted by esptool's cp932
progress-bar encoding exception; the UTF-8 rerun writes the identical app only
and verifies its hash (`event-probe-flash-utf8.log`). No FAT/NVS erase or write.
USB Shell becomes unresponsive after the first real-sleep-enabled transition.
Recovered RAM logs identify the IOE timeout; USB unresponsiveness alone was
not used as proof of an I2C fault.
The [official USB Serial/JTAG documentation](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-guides/usb-serial-jtag-console.html#sleep-mode-considerations)
explains clock gating in light sleep and possible need to unplug/replug after
wake. CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION can protect USB-attached debugging;
USB-free sleep must still be tested separately before claiming acceptance.

The final numeric battery-only confirmation is 3.750 V, 50%, Power battery,
Charging no, after the documented 30-second cache expiry. The human also
explicitly confirms that the intentionally selected z4 survives close/reopen.
All required physical observations have now been supplied; touch finishes OFF.
Final post-Writer FAT/NVS/checkpoint comparison passes (after-alt); another
comparison is required after subsequent diagnostic flashes. Shared-I2C
late pin-retention comparison passes, but the production-init probe still
fails. Shared-bus reliability and final persistent-data verification remain
outstanding.

Charging detail remains unknown when IP2315 cannot be read. No charger control,
capacity/ETA, calibration, current saving, battery lifetime, or long-term RTC
drift claim is made. Optional DMM/current measurement is NOT MEASURED until
equipment and actual readings are provided. Sketch integration and display
quality changes remain separate next phases.

### Further shared-I2C diagnosis (acceptance remains open)

The fourth probe's recovered RAM log records a shared-bus timeout during its
first real-sleep transition: IOE 0x4f/reg 0x06 returned 259, SDK TIMEOUT state,
after the optional charger NACK. Later natural recovery does not count as a fix.

Two additional trials also failed and are not adopted in production:

| Diagnostic app | Bytes | SHA-256 | First shared-bus failure |
|---|---:|---|---|
| `papermono-i2c-guard-probe` | 3339472 | `8c24b47cfdc70f8afa9688c0bed3ad8126fcdb2ae9ac5d6870fef0a20895e700` | IOE reg 0x06, software timeout, SDA LOW/SCL HIGH |
| `papermono-i2c-packed-probe` | 3339760 | `9cee56b1ee5676e60402dfe46d274d0a2bb323f974757f0cc008861a2dd947f0` | IOE reg 0x06 after one successful actual sleep; SDA HIGH/SCL LOW |

The former holds an outer NO_LIGHT_SLEEP lock across SDK device setup/transfer/
cleanup. The latter additionally packs short register writes into one transmit
buffer. Neither prevents the failure. The packed trial also returned success
with an incorrect IOE ID (00 instead of 5c); checking return codes alone is
insufficient. These probes used a temporary USB monitor PM lock. USB monitor
inactivity can occur while the cable remains physically attached, so these
runs are not described as battery-only tests.

A separate known SDK RX-completion race is backported through a PaperMono-only
build overlay, leaving the installed ESP-IDF package unchanged. Espressif's
[official fix 4118f4f](https://github.com/espressif/esp-idf/commit/4118f4f72d7983063490cc1981aced0273df602d)
moves the ISR completion notification after the RX FIFO copy; see
[issue 19050](https://github.com/espressif/esp-idf/issues/19050). This eliminates
a demonstrated cross-core stale-buffer interleaving in a compiled regression
fixture. Five new tests pass, including idempotence, unknown-source rejection,
build-copy isolation and exclusion of other boards. The full WSL Python suite
is now 559 tests with the same two failures/two errors/four skips described
above (`python-rx-overlay.log`).

The installed I2C source is byte-identical to
[official v5.5.5](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_driver_i2c/i2c_master.c),
SHA-256 `884ee968512d8e375cd863641a325c4c8d98a21fa302b9cbcc5627f156dda790`.
The overlay changes only the RX completion order described above.

The RX-fixed diagnostic app is 3337184 bytes, SHA-256
`b7def1325922bdf51a72bb329d7c3a17d6a177087d2bd614d6742ab4c2731167`.
It still fails after one successful actual sleep: IOE reg 0x06, SDK software
timeout, SDA HIGH/SCL LOW (`rx-fix-probe-first.log`). Therefore the RX fix is
not presented as the resolution of the remaining sleep fault. Outer locking
and packed writes are absent from this image.

The next comparison retains normal I2C GPIO operation on SDA47/SCL48 during
sleep using the documented `gpio_sleep_sel_dis` exception to global GPIO
isolation. Diagnostic artifact `papermono-i2c-pin-retain-probe` is 3339584 bytes,
SHA-256 `f0e62f6fd292532be9d01d6104a8e3c679c1fd7e1d795948021d9b6a1bac1b67`,
app range `[0x10000,0x33f540)`. Both API calls return ESP_OK. The recovered log confirms six completed 45-second logical suspends, alternating
requested touch OFF/ON, and 15,266 successful actual sleep entries, zero failed
entries. All six rounds read PM ID 50, IOE ID 5c, RTC flags 21:00:10 with valid
clock/backup bits; all return to performance with SDA/SCL HIGH. No shared-bus
fault is recorded. The optional charger NACK remains explicitly separate.
The harness armed when the USB monitor became inactive, before the human
physically unplugged USB; later cycles (3?6) run after confirmed cable removal.
Therefore this is six successful cycles, including battery-only cycles, not
a claim that all six began without USB. Evidence: `pin-retain-probe-results.log`.

Production now applies the same sleep-pin exception from the I2C bus lifecycle,
limited to the native port and SDA/SCL selected by the PaperMono manifest.
Owned teardown restores GPIO sleep isolation; borrowed shutdown preserves the
owner state. A compiled lifecycle test covers both ownership paths, alternate
pins/port, initialization failure rollback, and boards without the policy.
PaperMono-only build defaults retain the short-sleep IRAM options, disable
CPU/cache retention allocation, and use the SDK USB connection sleep guard.
The first two options make actual sleep viable with the unchanged 10 ms input
cadence. CPU/cache retention is disabled because allocation failed in this
PSRAM-heavy runtime; it is not presented as the proven I2C fault cause. Late GPIO
retention is the controlled difference between the earlier failing and passing
probes. The production-init variant still fails; adoption is not accepted yet.
No current or battery-life saving is inferred from successful sleep entry. All diagnostic
flashes write only the application and verify its hash. Temporary main/driver/
linker hooks and generated SDK settings are restored in the source tree after
artifact creation.

### Full-calendar validation after pin retention

The first normal pin-retained build boots and reconnects BLE successfully.
However `date`/`time` reject the RTC calendar. Direct I2C reads succeed and show
`17 49 00 00 00 00 26`: zero weekday/day/month. Flags remain `21 00 10`.
This is invalid calendar readback despite ESP_OK, not proof by itself of an
I2C timeout. Its origin (stored hardware state versus faulty receive data) is
not established; successful return codes alone are insufficient. The six-cycle probe above verifies
IDs and RTC control flags only; it is not proof of intact calendar/elapsed time.
The failure remains recorded in `normal-final-sleep-runtime.log` and
`normal-rtc-failure-raw.log`. No reset/retry is used to call it a PASS.

The standard generic `date 2026-10-09`, then `time 17:46:33` (PC JST wall clock)
setter restores a valid UTC calendar. Readback is `34 46 08 20 09 10 26`, with
flags `21 00 10`. `date`/`time` agree with UTC+9. After three normal-firmware
FT6336 enable/disable cycles, the calendar remains valid and advances to
08:47:49 UTC. The touch source transitions ready/not-ready, workers stop,
reset and PYG13 power return OFF, IOE PWM registers remain zero, BLE remains
connected. These are automatic observations; they do not replace real contact
or optical confirmation.

A further temporary two-cycle diagnostic uses the final production native-pin
policy and SDK sleep/USB settings, validating the full RTC datetime and clock
integrity after both requested touch OFF and ON. It fails on the shared bus. Artifact `papermono-calendar-sleep-probe` is
3339472 bytes, SHA-256 `f63b69c7b045208e193768ef6ed5a038380fd787eefeb2097b7aef67b6a2178d`,
app range `[0x10000,0x33f4d0)`, app-only hash verified. The recovered ring contains
repeated RTC SDK software timeouts (state 8/event 0), logical suspend still
active; first-fault entries were overwritten by repeated poll traces. Raw reads
can again return success with invalid bytes. A follow-up diagnostic limits
repeated traces and logs actual GPIO sleep-selection bits at native init, boot
and the first fault. The init-based policy is not claimed as a proven fix.
After diagnosis, the normal application must be restored and FAT/NVS compared.
Given the invalid calendar found during extended diagnosis, complete red-button
OFF retention must be confirmed again from the newly synchronized clock on
the final normal application; the earlier physical PASS remains historical.

### Native pin-state diagnostic and current blocker

The follow-up diagnostic `papermono-i2c-pin-state-probe` is 3339872 bytes,
SHA-256 `0c49bfc9966e226900fef6694fbb5516afad944f4ed9112e320edbc0adef641c`,
app range `[0x10000,0x33f660)`, application-only flash/hash verified.
Native init logs both GPIO sleep-selection bits as zero and both API results
ESP_OK at 73 ms. At 674 ms, before any actual sleep or suspend, PM1 reg 00
returns SDK software timeout, SDA/SCL HIGH; both sleep-selection bits remain
zero. Cold initializers for PM1/display/RTC then fail and the runtime falls
back to headless Shell while BLE still reconnects. This is a fault persisting
across software reset, not evidence that native GPIO retention was overwritten.
The earlier initialization-order hypothesis is not established and is not
adopted as an explanation. Evidence: `pin-state-probe-boot.log`.

Repeated traces are limited so the first failure stays in the RAM ring.
All temporary main/driver/linker hooks are removed again from production
sources after artifact creation. The device currently retains this diagnostic
state pending a human complete hardware OFF/ON comparison. Requested hardware
power cycling does not itself count as a reliability PASS. A normal build
with no diagnostic automation is prepared for restoration after that step;
final persistent-data comparison remains pending. Pin retention and final
sleep settings are candidates whose end-to-end acceptance is still open.

Latest normal host suite: 129 executable invocations PASS
(`host-final-sleep.log`). Latest Python suite: 565 tests, two unchanged failures,
two unchanged errors, four skips (`python-release-normal.log`); 11 added
I2C/configuration tests pass. An intermediate extra failure classified the
four-setting supplement as a complete board defaults file. The supplement was
moved to `scripts/papermono_sleep.defaults`, avoiding unrelated PPP/config
changes; the original PPP fixture passes afterward. Generated-config migration
also supports environment board selection, preserves unrelated settings,
is idempotent, leaves other boards unchanged, and refuses to edit defaults.
No source-text fixture was rewritten to mask existing failures.

### Normal restoration after the cold-power comparison

`build/papermono-platform-acceptance-ready/firmware.bin`: 3335104 bytes,
SHA-256 `668ba5575f51c0f1835d0df384a0da42c123ca336b02d6e4fcd7a017d0df08c3`, application range `[0x10000,0x33e3c0)`.
Normal/security-trace-OFF build passes (`normal-ready-after-diagnosis-build.log`).
Production main/driver/linker files match the saved normal sources exactly;
no `platform-probe` label is present in the binary. Supplemental sleep settings
and native-pin policy are reproducible; their end-to-end reliability is still
under investigation. This artifact was restored app-only after the requested hardware OFF/ON

comparison, with esptool hash verification. IOE/display/RTC attachment succeeds
again, performance profile is 240 MHz with automatic sleep OFF and no harness.
The calendar remains invalid until an explicit normal date/time setter is used.
Final FAT/NVS comparison has not yet been performed.

### Cold-power comparison with ZMK tests deferred

The human confirms complete red-button OFF/ON and USB reconnection. Captured
`pin-state-cold-boot.log` shows PM1/display initialization succeeds and native
sleep-selection bits stay zero. The diagnostic harness had already armed at
10.631 s while the USB monitor was inactive, before the host evidence reader
opened it. At 36.948 s, after 460 actual successful sleeps, RTC reg 1d fails
with software TIMEOUT/event ALIVE, SDA HIGH/SCL LOW. The first fault stops further
automatic transitions; this is a reproducible failure, not cold-recovery PASS.
RTC calendar readback later returns invalid `17 04 00 00 00 00 00` despite a
successful transfer code. The normal app was restored; it reads similarly invalid
`20 05 00 00 00 00 00` with no automatic sleep active. The evidence does not yet
establish whether calendar storage or bus receive data was corrupted.

The normal setters `date 2026-10-09` and `time 20:13:43` (PC JST) then return a
valid calendar (`44 13 11 20 09 10 26`, UTC), recorded in
`rtc-cold-repro-resync.log`. This is time-setting validation, not resolution of
the preceding bus fault. The human has turned ZMK OFF and explicitly requests
keyboard-dependent tests be skipped for now. Existing historical BLE evidence
is retained, but no new keyboard regression result will be claimed.

A separate diagnostic variant now compares input-only sleep pin configuration
with normal open-drain retention. It uses `gpio_sleep_set_direction(INPUT)` and
floating internal sleep pulls for SDA47/SCL48, retaining the board's external
2.2 kOhm pullups; normal peripheral routing remains unchanged. This tests the
hypothesis that releasing outputs during gated-peripheral sleep avoids the LOW
line fault. It is not a confirmed root cause or an accepted production fix.
The harness waits until at least 60 s before arming to permit boot evidence
capture, then runs the same 45 s suspend intervals, requiring actual sleep
success and a valid full calendar. This delay is diagnostic coordination only.

The input-only diagnostic is `build/papermono-i2c-input-only-probe/firmware.bin`,
3339856 bytes, SHA-256
`e92b3565d67e30763df0b8c47f101622534d073d666bf7a1263ef24bd6446808`,
app-only `[0x10000,0x33f650)`, hash-verified. Its boot/preflight records valid UTC
calendar, PM1 ID 50 and IOE ID 5c. However, at 97.195 s, **before any suspend or
actual sleep**, RTC reg 10 fails with software TIMEOUT/event ALIVE, SDA/SCL HIGH.
The preceding charger NACK was at 97.094 s. The harness arms at 98.548 s but
immediately stops with zero sleep attempts. Thus this variant supplies no clean
sleep-cycle result and is not adopted. Evidence: `input-only-probe-cycle1.log`.
Twelve explicitly addressed charger NACK/read-calendar/read-IOE sequences later
return valid calendars and ID 5c (`nack-idle-sequence.log`); NACK by itself is
therefore not a sufficient reproduction condition. The time ordering suggests
examining transaction/error cleanup, without establishing that as the cause.

The next bounded diagnostic records command-semaphore timeouts before the SDK
resets its indices. Artifact `papermono-i2c-command-stage-probe` is 3340336 bytes,
SHA-256 `ca86dd9b910b8ff205d6e311100c4a64c3025be12829ef477bb961649012ce29`,
app range `[0x10000,0x33f830)`, hash-verified. It restores normal pin retention;
all diagnostic source/helper changes are saved separately and reverted in the
production worktree after building. At 157.700 s RTC reg 1d fails after a charger
NACK at 157.499 s, while logically suspended at 80 MHz. Actual sleep counters
are still zero. SDA and SCL are LOW after the SDK's own error cleanup. The
pre-command-semaphore-timeout callback did not fire. Source inspection locates
this failure in the completion-event wait instead of the command-token wait;
an event-wait diagnostic will capture hardware state before the SDK reset.
Evidence: `command-stage-first-fault.log`. Successful `date`/`time` outputs
later are not treated as a resolution of the first fault.

The event-wait diagnostic is `build/papermono-i2c-event-wait-probe/firmware.bin`,
3340480 bytes, SHA-256
`156c55b7df99777c798e40b1acaa98f213cbf44b9cebc9fa7dd76d443f429f22`,
app range `[0x10000,0x33f8c0)`, hash-verified. It adds the same bounded
pre-reset callback to both command-token and completion-event timeout branches,
including clock, interrupt, FIFO and pin registers. No retries, recovery or
changed transfer timeout are added. The production main/driver/linker/helper
are restored and byte-compared to the saved normal files. Initially it reads a
valid calendar at 240 MHz with no actual sleeps; the human is requested to remove
USB for an objectively counted battery-only test. No result is implied by
building the artifact or asking for this physical step.

### USB-removal readback and bounded archive diagnostic

The human removed USB at approximately 21:42 JST and reconnected after the
requested seven-minute interval, confirming the Shell image remained visible.
Readback at 21:56 JST is preserved in
`event-wait-after-physical-unplug.log`: the logical suspend state is inactive,
the power profile is performance, and the reported light-sleep count is zero.
The available 256-row log contains later idle/BLE scan records rather than
the beginning of the diagnostic. It therefore cannot establish whether the
harness armed earlier or where it stopped. This round is **inconclusive**, not
a six-cycle PASS and not proof of a new I2C failure. At readback, the full RTC
calendar is valid (`21 56 12 20 09 10 26`, UTC), flags are `21 00 10`, PM1 ID is
`50` and IOE ID is `5c`.

A diagnostic-only artifact now retains the first pre-reset timeout and fault
records in a separate bounded RAM archive and repeats them once per minute
after stopping. Arming also requires a fresh PM1 source-register read showing
no external power, in addition to an inactive USB monitor and uptime >=60 s.
This prevents host monitor inactivity alone from starting the physical test.
Neither archive nor gate changes transaction timeout, recovery, retry policy,
BLE reconnect behavior or persistent settings. Production sources/helper are
restored to their saved normal versions after the build.

Artifact `build/papermono-i2c-archived-power-probe/firmware.bin` is 3340896 bytes,
SHA-256 `c4c940c28a094b1ff86898b30e050aa9c517d6479933e8e22ecd64c91e13c961`,
app range `[0x10000,0x33fa60)`. Build succeeded in 110.12 s. The first flashing
invocation encountered a Windows cp932 progress-output exception; the complete
app-only retry with UTF-8 output succeeded and verified the written hash
(`archived-power-probe-flash.log`). Initial readback shows external-source
register `05`, valid UTC calendar (`42 57 12 20 09 10 26`) and IOE ID `5c`.
This diagnostic is currently installed; it is not the final normal release.
The subsequent physical test completes all six cycles on battery power, with
ZMK OFF as requested. Arming occurs at 74.158 s with fresh PM1 source `04`
(external-source bits clear). Three touch-OFF and three touch-ON 45 s suspend
intervals each retain the logical suspend state and record actual successful
light sleep. The wrapper totals **13300 attempts, 13300 successes, 0 errors**.
All six full-calendar reads return valid date/weekday/integrity and advance
from UTC 12:59:46 to 13:05:40; every cycle reads PM1 `50`, IOE `5c`, RTC flags
`21:00:10` and returns with SDA/SCL HIGH. `COMPLETE cycles=6 touch_off=0` is
recorded at 487.324 s. No first-fault callback fires. Readback after reconnect
returns valid UTC 13:08:01, flags `21 00 10` and both expected IDs.
Evidence: successful USB-read tool output and
`archived-power-probe-positive-summary.json`; the human confirms Shell text
remains visible. This is a **PASS for this bounded battery-only six-cycle
diagnostic**, not evidence that the previous USB-associated failures are fixed.
The normal power-status CLI's zero explicit-sleep count does not override the
wrapper's actual automatic-sleep counts. Normal-release KEY1/RTC retention
repeat and final persistent-data comparison remain pending.

The normal release was subsequently restored app-only and hash-verified
(`normal-after-battery-probe-flash.log`): 3335104 bytes, SHA-256
`668ba5575f51c0f1835d0df384a0da42c123ca336b02d6e4fcd7a017d0df08c3`,
`build/papermono-platform-acceptance-ready/firmware.bin`. Preflight records
SolarOS 4.16.1 writerdeck, performance profile/240 MHz/no automatic sleep,
valid date/time and raw UTC calendar (`50 09 13 20 09 10 26`, flags `21 00 10`).
Touch is enabled through the standard expansion command for the remaining
physical KEY1/contact test; cleanup uses the standard display command.
Evidence: `normal-after-battery-probe-preflight.log`. No diagnostic harness
remains installed, and ZMK remains OFF.

The human subsequently confirms normal-release battery-only KEY1 suspend,
resume, a real contact after resume, and USB reconnection. The normal log
records suspend at 124.576 s and restoration of the performance profile at
176.477 s, with resume complete at 181.836 s. Readback shows touch ready,
RTC ready, valid UTC 13:12:46 and flags `21 00 10`; standard touch disable
stops the worker, asserts reset and switches PYG13 OFF. No I2C error is present
in this captured normal log. Evidence: `normal-battery-key-touch-results.log`.
This validates the normal physical KEY1 path on battery power; its explicit
sleep CLI counter remains zero because it does not count automatic sleeps.
The diagnostic wrapper is the separate evidence of actual sleep. The original
positive raw-log filename was inadvertently reused during later warm-fault
readback; the positive summary is explicitly transcribed from the preserved
tool response, not represented as a complete original raw-log backup. Future
readbacks require unique phase names and reject overwrites.

Before the final complete-OFF repeat, UTC is 13:13:12 with flags `21 00 10`
and the standard cleanup command is issued (`normal-rtc-off-preparation.log`).
No time-setting command is issued during this repeat.

The human reports complete OFF at 22:14 JST and battery-only ON at 22:18 JST,
with Shell visible and all red/green/blue LEDs OFF. Readback obtains UTC
`2026-10-09T13:18:30` (raw `30 18 13 20 09 10 26`, weekday bitmap `20`, flags
`21 00 10`). PC read interval is
`2026-10-09T13:18:30.772569Z` to `13:18:33.717465Z`; the RTC value lies within
the interval allowing its one-second resolution. Software LED status is OFF,
RTC/display/IOE are ready and touch is OFF. No time-setting command or network
clock synchronization is used. **PASS: normal-release complete-OFF retention
for the human-confirmed approximately four-minute interval.** Evidence:
`normal-rtc-off-results.log` and `normal-rtc-off-wall.json`. This normal repeat
supersedes the pending retention check, while earlier invalid-calendar
diagnostic results remain recorded rather than being discarded.

The last normal-release USB-attached/touch-OFF KEY1 round also completes.
The human explicitly confirms resumed Shell text is normal. Suspend is logged
at 96.445 s, performance restoration at 168.514 s and completed resume at
173.316 s; the recorded suspend duration is approximately 72 s (not exactly
the requested 45 s). Readback returns valid UTC 13:20:38, RTC flags `21 00 10`,
PM1 ID `50`, IOE ID `5c`, inactive suspend and ready display/RTC/IOE with touch
OFF. USB Shell responds without replugging. Evidence:
`normal-usb-key-results.log`. The optional IP2315 status warning remains the
documented charging-detail limitation, independent of successful shared-bus
reads. This is a normal-path PASS for this physical round. Earlier diagnostic
USB-associated first faults remain historical failures with unestablished
root cause; successful bounded rounds are not a proof of indefinite reliability.

### Final normal-release persistent-state audit

After collecting the non-resetting RTC/KEY1 evidence, application data is read
through the ROM loader without erasing or writing NVS/FAT. This readback resets
the processor; it is not used as a remedy or as evidence that a failed test
passed. Final phase `final-platform-normal` contains NVS SHA-256
`8b89a840ec24da0d92a691848bb11b753d268000640e0f0069ded070226316b6`
and FAT SHA-256
`58f4238d93e783350c4ddf54406581b96301e0f8f082189869e8ef9a2787ae6c`.

All 40 FAT paths remain present. Differences are restricted to the human's
intentional `/async-test.md` edit (12 bytes), its valid saved recovery record
(zoom=4), and Shell history. Protected startup/startup.before-fastest hashes
remain unchanged. The old reserved-marker count remains one; no new Alt marker
was inserted. NVS retains the same 38 keys, all five BLE bond-store entries and
the remembered peer exactly. User preferences remain unchanged.

The original strict comparator first rejects one additional SDK-owned value:
`phy/cal_data`. It is not silently ignored or rolled back. Separate inspection
finds a 1904-byte calibration record in both snapshots, unchanged embedded
PHY version and station MAC, unchanged `phy/cal_version` and `phy/cal_mac`,
and 96 differing opaque calibration bytes. Calibration SHA-256 changes from
`1792c8691e66433dff6da108662a4cad7572bc32a4af0c1c47b3621b911d6d3c`
to `6c43dff761ffe043ceab6c83664354bd4338346cc7108bde0b397b3fea5bf0fd`.
Installed SDK `esp_phy/src/phy_init.c` explicitly stores this blob when RF
initialization refreshes calibration; the configuration enables its storage.
This difference is consistent with an SDK calibration refresh, but its exact
trigger was not captured. It is not user configuration or a bond reset. See
[official RF calibration documentation](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-guides/RF_calibration.html).

The opt-in audit checks the calibration size/identity/version rather than
allowing arbitrary PHY changes. It otherwise preserves the strict original
comparator and the exact bond/peer checks. With that separately reported SDK
runtime difference, the persistent-state audit passes. WireGuard's monotonic
boot ceiling advances by 31 expected reservations and is never rolled back.
Evidence: `final-platform-normal-comparison.log` (initial strict rejection) and
`final-platform-normal-audited-comparison.log` (bounded calibration audit).

### Final normal USB soak: acceptance remains open

A bounded three-minute read-only USB soak then reproduces a fault in the
normal release. It stops at round 7, after approximately 21 s of explicit
reads (firmware uptime 280.115 s). Round 6 has valid calendar/flags and expected
PM1/IOE IDs. Round 7 first reads valid calendar/flags, then PM1 reg00 reports
SDK software timeout; IOE reg00 returns `00` instead of `5c`, and battery reads
fail. Without reset or replug, immediate evidence readback confirms PM1 and
IOE failures and an invalid RTC payload `00 00 00 00 21 00 00` despite a
nominal successful RTC-read status. Power is performance/240 MHz, automatic
sleep OFF, inactive suspend, explicit sleep count zero. Thus this is a real
normal-release shared-bus failure, not only a diagnostic-variant problem and
not an inference from the optional charger warning.

Evidence: `normal-usb-soak-summary.json`, `normal-usb-soak-006.log`,
`normal-usb-soak-007.log`, `normal-usb-soak-fault-evidence.log`. No reset is used
before collecting that fault evidence. The separately successful six battery
cycles, physical KEY1 rounds and complete-OFF RTC retention remain valid
bounded results. **Overall acceptance remains incomplete**, and shared-I2C/
RTC/display reliability cannot be upgraded to PASS from those positive rounds.

### Follow-up tracing after the normal-release failure

An app-only flash of `papermono-i2c-archived-power-probe` preserves FAT/NVS and
captures the fault retained through a warm processor restart. Its first SDK
timeout occurs at 680 ms, before any diagnostic suspend: PM1 address `6e`,
event ALIVE, command 2, transaction 4, no read copied. Raw interrupt `a212`,
enabled mask `5a8` and status `6600c011` are retained before SDK timeout cleanup.
The raw and enabled masks do not intersect; a masked SCL-state timeout is
present. This warm-start record is not proof of the initiating fault mechanism.
Evidence: `archived-after-normal-fault-warm.log`.

The human then performs the requested complete red-button OFF/ON and reconnects
USB with ZMK OFF. PM1/IOE IDs return normally, but raw RTC calendar is invalid
(`35 02 00 00 00 00 26`). The invalid calendar is recorded before standard date/
time setters synchronize it to the PC clock. Resynchronization is not counted
as a successful retention test or as a bus-fault fix. Evidence:
`archive-cold-start-after-normal-fault.log`, `archive-cold-calendar-resync.log`.
The unchanged diagnostic app subsequently completes 61 read-soak rounds in
183 s with correct calendar/flags/IDs. It remains healthy at a later readback;
this bounded result does not overturn the normal-release failure. Evidence:
`archive-clean-usb-soak-summary.json`, `archive-after-generation-build-failed.log`.

A diagnostic for overlapping ISR/next-transaction ownership is being prepared.
Its first build fails a lock-free-atomic capability assertion and is never
flashed. All four temporarily substituted source files are restored to their
normal backups. The hypothesis is not yet established on hardware. A DONE-only
host interleaving model is insufficient to justify a production notification
reordering: the TIMEOUT path can wake a caller on a command semaphore without
consuming the completion queue, so a naive unconditional reordering could
publish a stale event into the next transaction. No such fix is adopted.

The configured PSRAM toolchain workaround disables hardware atomic RMW and
therefore reports `ATOMIC_INT_LOCK_FREE=1`. Inspection with the actual compiler
flags confirms the diagnostic's aligned 32-bit load/store-only C11 operations
emit `l32i`/`s32i` and `memw`, without added helper calls or locks. The revised
diagnostic limits itself to ESP32-S3/Xtensa with checked atomic size/alignment.
The entire instrumented SDK and isolated tracing fragment are compiled to
assembly separately. Baseline SDK helper/critical-section references are
unchanged; isolated tracing has zero external calls. Evidence:
`astra-generation-probe-verified.s`, `astra-generation-probe-only-verified.s`.
This establishes suitability of the measurement implementation, not the
I2C-fault hypothesis.

The revised generation diagnostic builds successfully (116.99 s) and is
flashed application-only with verified hash. Artifact
`build/papermono-i2c-generation-probe/firmware.bin` is 3,341,984 bytes, SHA-256
`a5313835d1d3bd76edc6cac8e4e34be940e8326fde54278b913075d3e2d3a8d9`,
range `[0x10000, 0x33fea0)`. All four temporary source substitutions are restored
and hash-compared to the normal backups afterward. Initial three-minute soak
passes 61 rounds. At uptime 210 s the diagnostic has observed 24,961 transfer
generations and 25,039 ISR tail checks, with zero detected crossover. Evidence:
`astra-generation-probe-build-v2.log`, `astra-generation-probe-flash.log`,
`generation-usb-soak-summary.json`, `generation-after-usb-soak.log`.

The extended observation then retains a **fresh first fault at 341.111 s**,
not an inherited warm-start failure. RTC reg10 fails its final-event wait:
all commands prepared (`cmd=7`, `trans=6`, remaining count=0), event ALIVE,
RX bytes not copied. At timeout, raw interrupt is `202`, enabled mask `5a8`,
status `100cc000`, SDA LOW/SCL HIGH, and source clock active. The decoded
controller status is main Idle/SCL Start, TX FIFO count 3, RX FIFO count 0,
bus-busy clear. The three queued bytes match address-write, register and
address-read. No completion, NACK, hardware timeout or SCL-state timeout is
raw-pending at this first fault. This supports stalled progress at START,
before address/data consumption; it does not support an already completed
read whose RX copy is merely delayed. SDA LOW alone does not prove a slave
held it, since it may be the controller's START output.

The preceding optional charger NACK is logged at 341.009 s. The following
RTC timeout still has zero generation crossovers (47,074 generations, 47,166
checks). This narrows investigation toward the preceding STOP/reset/next-START
handoff, without yet proving causation. A later explicit IOE read fails at
555.733 s, stopping the extended soak at round 57. Existing SDK cleanup and
subsequent ordinary reads return correct IDs/calendar again without a user
reset or USB replug; the recorded failures are not reclassified as PASS.
No suspend or sleep occurred. Evidence:
`generation-fresh-fault-evidence.log`,
`generation-extended-usb-soak-summary.json`,
`generation-extended-usb-soak-057.log`.

A further bounded diagnostic records CMD versus EVENT timeout origin,
caller/ISR core distribution, pending interrupt state before enabling and
eight frozen IRQ-entry records. It retains the original hardware control,
timeouts, notification order and SDK error handling. Its assembly checks
also show no added calls, locks or atomic RMW helpers in isolated tracing.
This is measurement-only; the normal production I2C policy is not changed
on the basis of an unconfirmed hypothesis.

The V3 diagnostic also reproduces a first fault at **6.424 s**, before any
sleep or suspend. The last eight IRQ entries show the optional charger NACK
(address `0x75`), completion of mux-disconnect writes (`0x4f`), then a hardware
TIMEOUT on the RTC flags read (`0x32`, register `0x1d`). At that IRQ,
`SR=0x6500c311` decodes to SCL **Stop**, main **Send ACK**, RX FIFO **3 bytes**,
TX FIFO empty and bus busy. `RAW=0xa312` includes hardware TIMEOUT and the
masked SCL-state timeout. Thus this occurrence received the requested bytes
but failed to finish final NACK/STOP; it differs from the earlier START stall.

All 107 callers and 157 IRQ entries up to this occurrence ran on core 0.
No enabled pending interrupt was observed before startup, and no generation
crossover was recorded. This particular first fault therefore does not
support the proposed direct cross-core ISR-tail race. The Epson RX8130CE SCL
pin is input-only, so increasing the timeout cannot be justified as an RTC
clock-stretch requirement. Evidence: `generation-v3-first-boot.log` and
`generation-v3-after-user-cold-start.log`. Subsequent current reads eventually
become invalid; successful earlier reads do not override the archived fault.

The reference Writer's unmodified M5Unified `Power_Class.cpp` at commit
`a977e402a860229bd749b599f476b56258d0e035` waits at least 2 ms after charger
mux selection before readiness checks. SolarOS currently reads immediately.
A bounded diagnostic A/B adds only that elapsed wait inside the exclusive
battery session, retains unconditional disconnection and does not copy the
reference's 64 scans/retries or longer hardware timeout. This is a candidate
comparison, not an accepted production fix or a proven hardware minimum.
The first diagnostic build failed at CMake configuration due to a temporary
link-option quoting error; it was not flashed. Production sources were
restored before preparing the corrected diagnostic build.

That corrected 2 ms-only diagnostic built successfully (112.02 s) and was
app-only flashed with hash verification. Artifact size 3,344,016 bytes,
SHA256 `85f25653f94b28b3e3d99a9e2ebc6a4fc494125d004d42861553e86cfb5b74e7`.
Warm restart first failed on the initial PM discovery before charger access;
this was preserved in `mux-settle-first-boot.log` and is not a valid timing
comparison. The human then performed USB removal/red OFF/ON/reconnection.
After cold start, PM/IOE identity and RTC flags were valid, but the calendar
was invalid. Standard date/time setters explicitly resynchronized it before
testing (`mux-settle-calendar-resync.log`); this is not RTC-retention PASS.

The 2 ms candidate **FAILS**: at 67.469 s a charger one-byte read reaches an
EVENT wait timeout, no IRQ entry for that transaction, SDA LOW/SCL HIGH;
then the RTC flags read fails at 67.670 s with both pins LOW. The bounded
300-second soak stops after two rounds. Its first failed command reports a
real API read failure, not just interleaved-output parsing. Evidence:
`mux-settle-usb-soak-summary.json`, `mux-settle-usb-soak-002.log`,
`mux-settle-first-fault.log`. This wait is rejected as an acceptance fix;
production sources are restored. A research-only disconnected-charger NACK
control is prepared next to distinguish SDK/error handling from electrical
charger attachment. Charging remains unknown in diagnostic controls; they
are not release candidates.

The disconnected-charger control **PASSes 99 read rounds over 300.8 seconds**
(UTC 14:36:51–14:41:51). PM/IOE identity, RTC calendar and integrity remain
valid; the retained diagnostic does not freeze, and has no generation
mismatch. Repeated optional charger calls record SDK NACK events while the
mux remains OFF; the trace ring at final readback contains more recent RTC
entries, so individual charger NACK IRQ masks are not preserved separately.
At uptime 330 s it reports 38,964 generations, 39,052 IRQ checks, no pending
enable intersection and no crossover. This bounded result does not establish
absence of every possible SDK defect, but separates ordinary disconnected
NACK handling from the reproduced connected-branch failure.
Evidence: `mux-disconnected-nack-usb-soak-summary.json`,
`mux-disconnected-nack-after-soak.log`. Control binary 3,343,600 bytes, SHA256
`75719b0b311dfbf5834fc59056ecb286c7f5c9cf070d3fef1e954edd882194aa`,
app-only flash verified. A second control performs select/disconnect without
addressing the charger. It has built successfully; no production policy has
yet been changed on the basis of the first control alone.

The toggle-only control **FAILS its data-validity check at round 2**. It
returns calendar bytes `28 44 14 20 09 00 00` (zero month/year) despite an
API-success read; IDs/flags and subsequent calendar reads are correct. There
is no archived SDK fault/timeout and no generation crossover at readback.
This transient data corruption must be distinguished from the earlier
hardware/software timeouts; it does not prove that the RTC calendar was
persistently overwritten or identify an electrical mechanism. Evidence:
`mux-toggle-only-usb-soak-summary.json`, `mux-toggle-only-usb-soak-002.log`,
`mux-toggle-only-first-fault.log`. The last filename contains post-failure
readback, not a frozen SDK fault. Control artifact 3,343,808 bytes, SHA256
`e4d26df8a901ab53477aa40913b9ecbeec0f300379fd7bb8bf83c2d954eb6f55`,
app-only flash verified.

The resulting narrow production candidate omits **automatic runtime charger
mux selection and charger reads** entirely. Startup masked latch/drive/mode
configuration still keeps that branch disconnected. The provider continues
to acquire measured PM1 voltage and source bitmap in an exclusive session,
propagates their errors, and uses the same cache/averaging/percentage mapping.
External charging/completion is explicitly unknown; battery-only charging is
known false. PM1 rail configuration is no longer misused as evidence of the
separate charger's state. This sacrifices optional charging telemetry to
protect required platform services; it is not claimed as an SDK race fix or
a proven electrical root cause. The host provider test verifies no charger
access/no mux writes, retained voltage/source/estimate, unknown external
charging, battery-only noncharging, and PM-read failure propagation. It PASSes.
The uninstrumented production candidate is being built and remains subject
to live validation; the shared-bus acceptance remains open until it passes.

The uninstrumented candidate builds successfully in 113.91 seconds and is
app-only flashed with hash verification. Artifact:
`build/papermono-platform-safe-telemetry/firmware.bin`, 3,334,400 bytes,
SHA256 `3bfc6e112e80445ef34d1958217049a222e8d387180d4c3e43f0db0ea8ac33db`.
Production `main.c`, I2C backend, component CMake and RX helper contain no
temporary trace/sleep probe. Its first boot reports real PM/IOE IDs, valid
calendar/flags and 3.850 V / 61% estimated / external / charging unknown.
Manual cleanup is requested before the normal-release cold-start test.
No calendar resynchronization was needed for this candidate's first boot.

The human confirms normal-release **23:51 complete OFF → 23:55 battery-only
ON**, fresh Shell display, RGB LEDs OFF and subsequent USB reconnection.
Without a time setter/reset, RTC readback is `2026-10-09T14:55:59Z` against
PC start `14:55:59.675Z`; flags are `21 00 10`, weekday bitmap `20`.
This final candidate's four-minute complete-OFF retention PASS is recorded
in `normal-safe-telemetry-rtc-off-results.log` and
`normal-safe-telemetry-rtc-off-wall.json`. The normal-release 600-second
USB/calendar/PM/IOE validation starts immediately afterward; still pending.
That run **PASSes 200 complete rounds in 601.7 seconds**, UTC
14:56:02.602–15:06:04.258, on the uninstrumented artifact. Every round validates
the complete BCD calendar against PC UTC, RTC STOP/VLF flags, PM1 `50`, IOE
`5c` and battery access. No time setter, reset, retry or USB replug occurs
during it. Evidence: `normal-safe-telemetry-usb-soak-summary.json` and the
200 numbered round logs. The source and firmware remain unchanged afterward.
Touch is then enabled normally; IOE power/reset sequencing completes and
FT6336 reports ready. Normal-release battery-only human suspend/resume checks
are the remaining live reliability checks.

All **129 host executables PASS** on the changed provider
(`host-safe-telemetry.log`). The native WSL Python suite runs 565 tests with
the same pre-existing 2 failures, 2 errors and 4 skips identified earlier;
no new failures (`python-safe-telemetry-wsl.log`). An initial Windows-only
attempt is retained separately: it lacks curses/CMake and hits Windows
temporary-file reopening restrictions, so its 77 environment errors are
not compared to the Linux baseline. No production source is altered to
satisfy the four stale BLE/source-policy assertions. `git diff --check` PASS.

The four stale assertions are subsequently repaired **in tests only** to
check the current architecture/security contract: reconnect worker extracted
into its included source, layout delegated before legacy preference saving,
candidate freeze before mutation (diagnostic advertisement matching may
precede it), and HID repeat-pairing delegated to shared security without
automatic bond deletion. The shared backend's bond replacement remains
gated to an explicit pairing operation and its matching connection.
No BLE implementation/policy changes accompany this maintenance. All 42
runtime-boundary tests PASS (`python-boundaries-current-contract.log`);
the complete native Python suite is rerun after these test changes.
The resulting full suite **PASSes: 565 tests run, 4 skips, no failures/errors**
(`python-current-contract-final.log`, 18.719 s). Its intentional unpatched-SDK
negative control prints an assertion failure before confirming the patched
security sequence PASS; that is an expected child-process result, not a suite
failure. The flashed production artifact is unchanged by test-only edits.

The final uninstrumented **touch ON / battery-only KEY1 round PASSes**.
The human confirms display resume, a real contact/release and USB reconnection.
RAM readback records suspend at 761.033 s, performance restoration at 817.799 s
and completed display resume at 822.972 s. Touch is ready, with two pointer
events and a final release/buttons=0; the calendar and integrity flags remain
valid, PM1/IOE IDs are `50`/`5c`, and battery reads report 3.832 V / 59% estimated.
Evidence: `normal-safe-telemetry-touch-on-resume.log`. No reset or USB replug
is needed to collect these results. The normal release's explicit light-sleep
counter does not count FreeRTOS automatic light sleep; it is not used as a
measurement of the automatic sleep transitions in this physical round.
Touch is disabled normally afterward: ready=no, pointer count remains two,
last release/buttons=0, and calendar/flags/IDs are still valid
(`normal-safe-telemetry-touch-off-prepare.log`).

The corresponding final **touch OFF / battery-only KEY1 round also PASSes**.
The human confirms normal Shell display after resume, no unintended operation
from the OFF contact and USB reconnection. RAM logs record suspend at
1256.733 s, performance restoration at 1318.899 s and display resume at
1323.544 s. After a real OFF contact, touch remains ready=no with exactly the
same two pointer events and final release/buttons=0. RTC calendar/flags and
PM1/IOE IDs remain valid; USB Shell responds on the first collection attempt,
with performance restored and suspend inactive. Battery reads 3.844 V / 60%
estimated / external / charging unknown after reconnect. Evidence:
`normal-safe-telemetry-touch-off-resume.log`. No extra reset or replug is
used to recover this test. Both physical rounds' RAM evidence is saved before
the final read-only FAT/NVS snapshot, whose ROM exit does reset the application.

The final read-only persistent audit **PASSes**
(`final-platform-safe-telemetry-comparison.log`). The snapshot reads NVS
`0x9000`/16,384 bytes and FAT `0xe10000`/2,031,616 bytes; ROM exit resets the
application only after the physical-test RAM evidence is saved. NVS SHA256 is
`9def65213baeaa5c87913b570d6926d24d33887e75210e2f2fc084a2a8744a51`;
FAT SHA256 is
`10af64b1997ad304c494ac61ac6ed9a393a42be282aac3a51f9960eee93d1238`.
All 40 file paths remain. Only Shell history and the explicitly intended
`/async-test.md` edit/checkpoint differ from the baseline: the user intentionally
deleted the old text; the resulting 12-byte document retains its two diagnostic
X suffixes and the original single reserved byte, with no new Alt marker.
Saved checkpoint checksum is valid and zoom=4. Both startup files are
byte-identical to baseline. All 38 logical NVS keys remain, with five bond keys,
remembered peer and user settings unchanged. Expected exceptions are 42
monotonic WireGuard boot reservations and SDK PHY calibration: 107 opaque bytes
differ within the same 1904-byte record, while its version/MAC identity and
separate calibration version/MAC keys remain unchanged. This is logical user
state preservation, not a claim of byte-identical entire NVS.

After the snapshot resets, normal-release readback still shows SolarOS 4.16.1 /
writerdeck, active Shell, preserved startup `refresh=fastest`, JP keyboard,
mono 20px and UTC+9, all expected document paths, remembered keyboard 1/1
(ZMK intentionally OFF), LEDs OFF, touch not-ready with zero boot events,
performance/suspend inactive, valid RTC calendar/flags and PM1/IOE IDs.
Battery reports 3.842 V / 60% estimated / external / charging unknown.
Evidence: `normal-safe-telemetry-final-state.log`. No remaining required
physical check is pending. Optional external voltage/current measurements and
the explicitly excluded next phases remain outside the completed acceptance.

### Original physical checklist (completed results recorded above)

Round 1 (USB attached): run `version` and `battery` on the display Shell so
the plug icon is rendered legitimately; touch the screen while OFF. Enable
`touch0` and hold/release upper-left, upper-right, lower-left, lower-right in
landscape (USB-C upper left side) for 4 seconds each, with 3 seconds between.
USB diagnostic capture pairs real ordered contacts with logical coordinates.
Pretype `expansion disable touch0`, then hold the screen and press Enter while
still holding; release afterward. Perform two more real enable/contact/release/
disable cycles. Enable and KEY1 suspend/resume once, touch after resume;
disable and KEY1 suspend/resume once. Finish OFF. PASS requires correct corner
orientation, working resumed contact, no activity while OFF, and no stuck press.
KEY1 presses are about 0.8 seconds, below the 1.2-second pairing threshold;
wait 10 seconds between transition checks, without changing production pacing.

Round 2 (USB removed, battery only): leave ZMK ON with unchanged profile/bond.
After removal wait **30 seconds only for the documented PM1 cache**, then run
`battery`/`version` and observe normal battery fill (no fuel-gauge-absence slash).
Record voltage, estimated %, Power, Charging. Run `date` and `time` and compare
to an external wall clock. If time is incorrect, stop and report it before the
retention test; do not silently change timezone or enable network time.
Run `display mode display0 refresh=cleanup`, allow full cleanup to complete,
then completely OFF via the red hardware button. Note wall-clock OFF time,
wait at least 3 minutes, boot on battery without USB or network assistance.
Immediately run `date`, `time`, `version` and record wall-clock ON/read time.
PASS: valid restored calendar, real elapsed time retained within a few seconds
for this short test, automatic remembered keyboard input, and red/green/blue
LEDs visually OFF after stabilization (no unexpected light). The cleanup is
the previously established workaround for red-OFF image grain, not a new
shutdown implementation. KEY1 suspend is not this retention test.
While PaperMono remains ON, turn ZMK OFF for at least 20 seconds (one real
disconnect), then ON and execute `version` without re-pairing/reboot/profile
changes. PASS requires automatic remembered reconnect and functioning input.
Reconnect USB afterward for RTC flags/source/log readback.

Round 3 (existing Writer data): `ls`, then open an actual existing document
(e.g. `/transport-test.md`, present in the baseline, not a replacement).
Confirm its saved text and retained zoom, append a small identifiable test
suffix, and Ctrl+S. Alt+Tab to Shell and back; KEY1 suspend/resume and confirm
text/input. Save and exit/reopen the same document to confirm persisted suffix
and zoom. Confirm normal fast updates and `refresh=cleanup` with no new
corruption. Finish touch OFF. Actual intended edits are tracked separately
from the byte-identical post-flash baseline; do not restore an older snapshot.

Optional measurements: only with suitable equipment already available and a
safe battery measurement point. DMM comparison records simultaneous external
and CLI mV/offset without calibrating from a single reading. Current tests
separate steady Shell/BLE idle touch OFF/ON (and safely controllable LED states)
from refresh/reconnect transients, recording setup, duration and min/median/max.
If no equipment is available, these remain NOT MEASURED.
