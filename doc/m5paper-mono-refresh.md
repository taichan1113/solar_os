# PaperMono OTP fast refresh investigation and acceptance

The later opt-in `refresh=fastest` host-LUT implementation and its quality/power
tradeoffs are documented in [the Writer strategy comparison](m5paper-mono-writer-strategy.md).
The OTP AUTO/partial behavior measured in this report remains available.

## Phase 0 — recorded before implementation (2026-10-07)

SolarOS base: `1ba98726e0c52f5147c8a565522a94d7b67b0291`, with the existing,
uncommitted PaperMono port. Reference checkouts are kept in ignored `.research/`,
not vendored or added as dependencies. Remote HEADs were checked on this date.

| Reference | Revision | Role |
|---|---|---|
| [OTP Demo](https://github.com/m5stack/M5PaperMono-OTP-Demo/tree/c7c02554f89fd06f80d988b805b2a59050c78a46) | `c7c02554f89fd06f80d988b805b2a59050c78a46` | Primary authority: `components/EDP_OTP_LUT_demo/src/EDP_OTP_LUT_demo.cpp`, `EDP_SPI.cpp` |
| [M5GFX](https://github.com/m5stack/M5GFX/tree/c5a3fefad0b38a52cc750e2c4761e339a69a7208) | `c5a3fefad0b38a52cc750e2c4761e339a69a7208` | Compare `Panel_SSD1677.inl` and four-gray implementation |
| [UserDemo](https://github.com/m5stack/M5PaperMono-UserDemo/tree/c1099107271d31a0678d661a896e2b04dbb331ea) | `c1099107271d31a0678d661a896e2b04dbb331ea` | Application context |
| [PowerDemo](https://github.com/m5stack/M5PaperMono-PowerDemo/tree/cf4f57c638aa3ad0168bf90beebdaa3599203bb2) | `cf4f57c638aa3ad0168bf90beebdaa3599203bb2` | `tests/display_refresh_modes/README.md` timing comparison |
| User's `E:\ESP32\M5Paper_Writerdeck` | `86877a881a89b4867c561899f42bdfecd973af1b` | Known working application; `src/WriterDeckPaperApp.cpp` |

The local Writerdeck uses registry M5GFX **0.2.28**, not a separate Git checkout.
Its `Panel_SSD1677.cpp` SHA256 is
`c87a4c6a95a24d50d4d2750fe99215c655d673b040337523e3ace13fba093f58`.
Its whole-screen/dirty-line policies and partial budget of 128 are application
choices, not an OTP cleanup recommendation. They are not copied into SolarOS.
M5Stack reference code is MIT; the existing
`src/drivers/m5paper_mono.LICENSE` retains M5Stack attribution for the register
sequence. No Arduino, M5GFX library, waveform table or voltage tuning is imported.

### Classification and register comparison

OTP Demo `refresh_partial()` sends the **entire 800×480, 1 bpp frame (48,000
bytes)** to RAM 0x24 and activates a fast differential waveform. Although its
window helper can align/clamp rectangles, this refresh function always requests
the full screen. It is optimization 1, not evidence for optimization 2.

| Operation | Existing SolarOS / OTP full | OTP partial to implement |
|---|---|---|
| Wake | Rail on, hardware reset low/high 10 ms each | Retained powered rail, hardware reset low/high 10 ms each |
| Software reset | `12`, wait ready | **Omit** (preserve baseline) |
| Temperature | `18=80` internal sensor | Retained setup; OTP activation selects temperature/LUT handling |
| Booster/gates | `0C=AE C7 C3 C0 80`, `01=DF 01 02` | Retained setup; no new voltage writes |
| Border | `3C=01` | `3C=80` floating |
| RAM scan | SolarOS `11=01`, ascending X, descending Y | Same SolarOS scan and full window |
| Stage 1 | `22=F8`, `24=~image`, `20`, BUSY wait | `24=image`, `21=00`, `22=FF`, `20`, BUSY wait |
| Stage 2 | `22=14`, `26=image`, `24=image`, `20`, BUSY wait | None |
| RAM control | `21=00` | `21=00` |
| LUT | Built-in OTP; no `32` upload | Built-in OTP; no `32` upload |
| Finish | `10=01`, delay 100 ms | `10=01`, delay 100 ms |

BUSY is active high; every wait is bounded at 30 seconds in SolarOS. Keep the
existing conservative BUSY sampling initially. OTP Demo waits 1 ms before
polling, whereas SolarOS waits 100 ms then samples every 20 ms; measured BUSY end
is consequently a sampled upper bound, not an oscilloscope edge timestamp.

RAM 0x24 contains the incoming image. RAM 0x26 participates in establishing the
previous/baseline image during full refresh. OTP Demo does **not** write 0x26
after partial activation: repeated partial depends on the controller's OTP
sequence handling comparison state. Do not import M5GFX's explicit post-update
0x26 synchronization into this different sequence. Internal silicon copying is
not independently proven by this code inspection.

OTP Demo documents RAM retention in deep sleep mode 1 **while EPD power remains
on**. Hardware reset wakes it; software reset is deliberately absent from
partial. Host shadow equality alone is not evidence of retained controller RAM.
Unlike the demo's boot white-RAM preload, SolarOS will establish its initial
baseline with a completed visible full update, even when the requested image is
white. Reboot, rail removal, software reset, suspend/resume and any refresh error
invalidate the baseline. Successful powered refresh sleep preserves it. Explicit
MCU sleep through the normal display power-save path invalidates it; automatic
MCU idle sleep alone does not remove the EPD rail.

The controller window is native 800 sources × 480 gates. Portrait UI coordinates
480×800 and the logical landscape 800×480 are handled by U8g2 R3 and the existing
terminal 270-degree configuration. Keep this orientation. SolarOS `44=00001F03`,
`45=DF010000`, `4E=0000`, `4F=DF01` starts at gate 479 and descends; OTP Demo uses
entry mode 03 and ascending gates. X window boundaries are bytes (8 pixels).
Do not change addressing direction without changing framebuffer conversion.

### Comparison, limits and policy

Current M5GFX monochrome supports bounding-window writes, modes quality/text/fast,
and explicit RAM synchronization; its four-gray subclass uploads mode-specific
LUTs. These are different paths from OTP Demo. A newer implementation is not
proof of improved physical stability. PowerDemo reports M5GFX fastest 1.192452 s,
fast 1.375295 s, quality 4.557625 s at 3.7 V; these are **their measurements**, not
SolarOS OTP measurements or guarantees.

OTP Demo specifies no fixed repeated-partial cleanup interval. PaperMono will
use explicit full cleanup, not inherit generic SSD1677's arbitrary 20-update
policy. Unknown baseline falls back to full; hardware/transport failures latch
and stop instead of automatically retrying full. Full remains the initial
default until standalone physical acceptance. Cursor blink stays disabled.

## Implementation and acceptance

### Driver and CLI

`papermono_baseline_valid` is separate from `shadow_valid` and `controller_ready`.
`controller_ready=false` after a successful refresh means powered mode-1 sleep,
not loss of baseline. Partial eligibility additionally requires `powered`, a
valid host shadow and its allocation. A refresh first invalidates baseline,
performs the selected sequence, then restores it only on complete success.
No-shadow allocation failure therefore degrades to full. All public power-save,
resume and software-reset paths invalidate baseline. Failures latch until a
fresh driver instance/reboot; unknown baseline with healthy hardware falls back
to full. An identical framebuffer is skipped only with valid baseline.

`powered` records the driver's rail control state, not continuous electrical
telemetry. An out-of-band rail drop that the MCU cannot observe is not detectable
from framebuffer equality; after any such suspected event use full/reinitialize.
The explicit MCU sleep path in `src/main.c` calls display power-save before sleep
and the SSD1677 resume hook after wake, so it takes the same full-baseline path as
suspend. This is code/host coverage until explicitly marked physically tested.

`refresh=full` selects the original full-only policy. `refresh=partial` and
`refresh=auto` select fast with safe full fallback; PaperMono has no fixed
periodic cleanup counter. `refresh=cleanup` queues one full update on the next
present, including an identical image, without changing the selected policy.
Mode changes only request policy; the next normal display present owns SPI I/O.

From USB shell, with display applications idle and no keyboard typing:

```text
log sink cdc on
display mode display0 timing=on
display mode display0 refresh=full
display test display0 white
display mode display0 refresh=partial
display test display0 box-left
display test display0 box-right
display test display0 white
```

Then alternate `box-left`/`white` at least ten round trips, inspect `corners`,
`fine`, `half`, `black`, `white`, and inspect the final white **before cleanup**.
Each command presents exactly once. The existing display claim protects targets
owned by graphical applications. Do not type into a display terminal during
this standalone test (the terminal would replace the pattern). Static patterns
are reusable diagnostics, not an automatic startup test or a Writer code path.

```text
display mode display0 refresh=cleanup
display test display0 white
display mode display0 timing=off
log sink cdc off
```

Cleanup can also be queued while Writer is active: its next present uses full.
This is a pending request, not a claim that a refresh has already occurred.
For immediate cleanup on an unchanged screen, use the standalone test present.

Timing logs use `esp_timer_get_time()` microseconds and record every RAM write
start/end, activation start/sampled BUSY-ready, and refresh start/end. RAM write
interval includes U8g2 row conversion, window commands, SPI and driver overhead;
it is not a logic-analyzer measurement of MOSI-only time. Total includes wake,
setup and the required 100 ms sleep-settle delay. Failed intervals are labelled
with a nonzero `err`; they are not successful panel measurements. Timing is
off by default and does not log unchanged/skipped frames.

### Validation results

- Python: 538 tests, 534 passed / 4 skipped.
- Metadata: all boards valid; both PaperMono manifests valid.
- Host: SSD1677, UC8179, UC8279, display layout, terminal geometry, input,
  keys, BLE HID, FT6336 and PaperMono power tests passed.
- SSD1677 coverage includes exact fast register order/window/48 KB RAM24 data,
  25 successive partials without generic periodic full policy, full baseline,
  identical skip, forced identical cleanup, power-save/unknown-RAM/no-shadow
  fallback, transport errors at RAM/control/activation/sleep and stuck BUSY.
- Phase-1 normal + bringup builds succeeded with ESP-IDF 5.5.5.
- Phase-1 flashed image SHA256:
  `5a7f5331cb47103ff6b8e638782b19a9bf240a68853237d4f5eac88659acc804`.
  Bringup build (not flashed):
  `d397f2f9f6ccdb9abcef0aa22d5a6a4022093ced0465a577f1803e54593202bf`.

Initial hardware measurements: full white 4,478,959 us; RAM writes 68,454 /
65,375 / 65,278 us; activations 99,697 / 3,819,281 us. First fast box:
857,383 us total, 65,351 us RAM write, 559,426 us activation.
The user confirmed the first black box appeared without visible anomalies.
First logs: `.research/hardware/refresh-phase1-boot.log`, `refresh-fast-first.log`.
The previous full-only port was approximately 4.5 s, consistent with the retained
full sequence. The following records cover the subsequent quality tests and
Writer integration.

Standalone follow-up: at least ten black-box/white round trips plus corners,
center, fine text/lines, half-screen and black/white transitions completed.
The user reported **no visible ghosting or edge artifacts before cleanup**.
Manual cleanup of the identical final white frame completed in 4,478,987 us
(three RAM writes 68,459 / 65,307 / 65,267 us; activation 99,712 / 3,819,353 us).
This confirms fast→full and cleanup bypass of the identical-buffer skip.
Logs: `refresh-move-white.log`, `refresh-repeat20.log`,
`refresh-repeat-and-patterns.log`, `refresh-cleanup.log` in `.research/hardware/`.
The first repeat script stopped after matching BLE's `supervision_timeout`
parameter as a generic timeout string. This was a harness false positive;
the original log is preserved. Panel failure matching was narrowed to actual
BUSY/fault messages, and the remaining repetitions completed. No panel retry
following a real hardware error occurred.

After this visual acceptance the driver default was changed to AUTO for normal
display presents. Writer-specific rendering and dirty rectangles are unchanged.
No custom waveform, voltage registers or blink re-enable was introduced.

Final normal image written to COM39:
`52283a369464cae9e936eec23fad047c43fd08fd93299da68f546e070633922b`.
`build/papermono-writerdeck/build-info.json` records all artifact hashes; the
factory image's application bytes were compared against `firmware.bin`.
The original full-only application was retained in `build/papermono-full-only/`.
Boot reports `refresh=auto`; Writer `/otp-refresh-test.md` opened through the
normal display service with an 858,178 us fast update.

BLE reconnect caution observed during this acceptance: remembered count remained
1/1, but the hard-reset uploads were followed by repeated 60-second HID-open
timeouts before connection. Phase-1 eventually connected without bond deletion;
the final Writer test requested a ZMK OFF/ON on the same profile. These events
are kept in `refresh-repeat-interruption.log` and `refresh-writer-input.log`.
No BLE code changes were made in this refresh phase. Reconnect reliability must
not be reported as an unqualified pass on the strength of display success.

### Writer BLE typing measurements

Final firmware, existing ZMK bond, normal Writer session 1. User confirmed all
26 requested characters appeared without omissions and the wait improved.
`refresh-writer-input2.log` contains the existing BLE debug report timestamps
and driver timing records; no BLE instrumentation patch was needed.

| Input | Refreshes | First input processing → final refresh end | Last key processing → final refresh end |
|---|---:|---:|---:|
| `a` | 1 | 0.899058 s | 0.899058 s |
| `bcdef` | 4 | 4.027986 s | 2.992986 s |
| `12345678901234567890` | 7 | 6.891986 s | 2.406986 s |

Input timestamps are from the BLE keyboard service's report processing, on the
same monotonic clock as the driver; resolution is 1 ms. They are not physical
switch-down or over-the-air timestamps. Batch totals include the user's typing
time and settle at the final observed frame before the next input batch. These
do not imply 0.9 s for an entire 20-character burst. Updates batch queued input,
with approximately 0.858 s per actual panel update. Writer idle/render ticks
still show approximately 220–250 ms overruns; fast waveform does not remove
common rendering/runtime costs.

Final bringup build also passed, SHA256:
`8d920a66e3a0f5f1280209878f0907d27f2711ace52a41fc3d624e1028772f63`.
It is saved under `build/papermono-bringup/` and was not flashed. Final Python
suite again passed 538 tests (4 skipped). SSD1677 additionally passed with both
1 ms and 10 ms simulated ticks, including public resume and mid-plane SPI failure.

Writer edit acceptance: the user confirmed Enter, Backspace, left/right cursor
movement, adding lines until scrolling, and typing `END` all work normally.
`refresh-writer-edit.log` records these BLE reports and their updates. The first
isolated Enter report was at 886.711 s; the first following frame ended at
887.614993 s (0.903993 s), and the final update before the Backspace ended at
889.545989 s (2.834989 s). Backspace was at 890.320 s; the final update before
cursor input ended at 893.446990 s (3.126990 s). Cursor presses and subsequent
Enter/repeated-line input overlap in this capture, so a separate single-cursor
end-to-end latency is not inferred. Their actual panel refresh times remain
approximately 0.86 s. User confirmation establishes behavior, not isolated
per-key latency for those overlapping actions.

### Change boundaries

This phase changed `src/drivers/epd_ssd1677.c` / `.h`,
`tests/host/epd_ssd1677_test.c`, `src/shell/solar_os_shell_commands.c`,
`doc/manual/commands.md`, `doc/m5paper-mono.md`, this document, and the blink
policy comment in `boards/manifests/m5paper_mono.toml`. Existing display service
mode callbacks and present APIs provide integration without additional service
code. The earlier uncommitted port/input/BLE changes are still present; they
must not be mistaken for changes made by this refresh phase.

Dirty RAM rectangles and Writer dirty-region propagation are **not implemented**.
Full-frame differential waveform is implemented and physically tested. Full
transfer is about 65 ms of an 858 ms refresh (under 8%); optimize waveform first
has delivered the main improvement. Any next rendering/dirty-region work should
address measured common rendering/queue costs through display abstractions,
not PaperMono coordinate logic in Writer.

### Suspend observations and limits

The first 0.3-second button trial did not give an unqualified button pass: the
user saw no KEY2 newline, and `input test buttons` confirmed zero key events.
One KEY1 suspend was logged, but wake was triggered by subsequent BLE typing,
not by a logged KEY1 resume. The unchanged e-paper image during suspend is
normal; it is not evidence that suspend did not happen.

That transition removed the EPD rail (`PYG3=0`) and invalidated baseline.
Resume rebuilt it with a **4.480619 s full** update, followed by an **0.858723 s
fast** update for editing. A later 0.8-second KEY1 suspend followed by BLE `a`
wake again produced **4.479899 s full → 0.857503 s fast**. USB capture stayed
usable through these cycles. This physically proves rail-off fallback and
return to fast; it does not establish reliable 0.3-second system/generic KEY
polling. The shared KEY architecture is intentionally unchanged.

The user subsequently confirmed KEY2 held about one second and released inserts
a newline. A separate pair of approximately 0.8-second KEY1 presses produced a
logged `KEY short press: resumed` (no BLE wake), with full refresh **4.478381 s**.
This distinguishes successful standard system-KEY operation from the unreliable
very short trial. The baseline is deliberately rebuilt on every rail-off resume,
so the first visible wake still takes about 4.5 s.

Detailed review of the same button capture also found **two long-press events**
at 1583.920 s and 1598.525 s, before the successful short-press pair. The standard
handler removed the keyboard bond/remembered state and requested new pairing.
The old ZMK profile then failed to pair, and `ble status` reported remembered
0/1. Physical hold duration was not instrumented, so this log cannot distinguish
a longer physical hold from scheduling/debounce effects. It must not be reported
as clean short-press acceptance. Long-press semantics themselves match the
standard KEY policy. Recovery used USB pairing after clearing only the PaperMono
ZMK profile, as recorded below; no NVS-wide erase or changes to the common
KEY/HID implementation were involved.

Recovery completed: the user cleared **only the PaperMono ZMK profile**; USB
`ble pair` established a fresh connection in about 13 seconds. Status again
reports `connected travelers-split`, remembered **1/1**. The existing Writer
session remained open. Logs: `refresh-repair-pairing.log` and
`refresh-repaired-status.log`. The user also confirmed that the KEY-driven full
wake retained the displayed text. KEY2 diagnostics now count two key events
(press/release) with key 10, consistent with the confirmed newline.

### Final acceptance and timing distribution

After re-pairing, the user confirmed that ZMK could append `Z` to the retained
Writer session. The first resulting fast refresh took 858,102 us; from the BLE
key report to that frame's completion was 1.039058 s. The user then completed
four corner touches with landscape orientation and USB-C on the upper part of
the left side. Diagnostics advanced from 2 to 10 pointer events (four new
press/release pairs). Coordinates agree with the existing dispatch transform
`(logical_x, logical_y) = (799 - raw_y, raw_x)`:

| Touch order | Raw coordinate | Logical coordinate |
|---|---|---|
| Top left | (63, 714) | (85, 63) |
| Top right | (81, 70) | (729, 81) |
| Bottom left | (400, 694) | (105, 400) |
| Bottom right | (400, 76) | (723, 400) |

This verifies input events and orientation, not launcher icon hit-testing.
Evidence: `.research/hardware/refresh-touch-final.log`.

Aggregate successful timing records exclude duplicated `*-console.log` capture
output. Each phase is measured in microseconds; write and activation columns
sum all phases of one refresh. These are diagnostic samples, not a controlled
benchmark across temperatures or a latency guarantee.

| Mode | Samples | Total min / median / max | RAM write min / median / max | Activation min / median / max |
|---|---:|---|---|---|
| Full | 5 | 4,478,381 / 4,478,987 / 4,480,619 | 199,033 / 200,356 / 201,378 | 3,918,730 / 3,918,978 / 3,919,728 |
| Fast | 95 | 854,609 / 858,502 / 1,354,944 | 62,212 / 66,023 / 442,771 | 559,062 / 559,538 / 560,638 |

Median panel update time decreased by approximately **81%**, from 4.479 s to
0.859 s. One final fast update took **1.354944 s**: its RAM write interval grew
to 442,771 us, while activation remained 559,616 us. The write interval includes
conversion, SPI and scheduling; the cause of this outlier was not isolated.
It must not be presented as measured pure SPI transfer time, or hidden behind
a claim that every fast update finishes in 0.86 s. All recorded refreshes had
`err=0`; physical BUSY/power faults were not injected.

Final device state was verified through USB: `refresh=auto`, timing off, log
level info, CDC log sink off, suspend inactive, BLE connected and remembered
1/1. The Writer session remains open with its test text. A USB-injected Ctrl+S
did not create `/otp-refresh-test.md`, so this acceptance does **not** claim a
saved document or saved-file persistence. It establishes live session/content
retention across suspend. All diagnostic capture processes finished and COM39
was released. Final-state evidence is in `refresh-final-state.log` and
`refresh-saved-document.log`.

### Remaining work

- Investigate common runtime scheduling and system KEY polling: short presses
  were missed, and long-press classifications occurred during the button trial.
  Fast waveform alone does not establish reliable short-button detection.
- Investigate intermittent BLE HID-open timeouts during reboot reconnection.
  Fresh pairing and subsequent input succeeded; reboot reconnection after that
  fresh pairing has not been retested. The existing ENC_CHANGE/CONNECT fix was
  not modified by this phase.
- Measure rendering/queue latency and the 1.35 s outlier before deciding whether
  dirty RAM rectangles are worthwhile. Neither rectangular panel refresh nor
  Writer dirty-region propagation is implemented or claimed here.
- Extend ghosting tests across temperature and prolonged use. No fixed cleanup
  period is inferred from the current successful repetitions. Manual full
  cleanup and full-only policy remain available.
- Explicit MCU light sleep has code/host coverage, but separate physical
  acceptance remains outstanding; tested suspend cycles did remove the EPD rail.

The requested fast-refresh implementation and its standalone/Writer acceptance
are complete. Input/reconnection limitations above prevent an unqualified
claim that every regression criterion passed.

Follow-up clarification for the pipeline phase: the user reports that physical
button operation/hold durations likely differed from the instructions. The
captured button events above remain observations, **not established system KEY
implementation defects**. Hold durations were not measured. The next phase
prioritizes [Writer/display profiling](m5paper-mono-pipeline.md) and does not
change or separately investigate button architecture/debounce/thresholds.
