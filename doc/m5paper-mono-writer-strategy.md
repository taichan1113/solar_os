# PaperMono Writer display strategy comparison

## Task validity review — before implementation (2026-10-07)

Proceed with an opt-in diagnostic prototype, not an assumed rectangular refresh
port. Authoritative comparison workspace: `E:\ESP32\M5Paper_Writerdeck`, HEAD
`86877a881a89b4867c561899f42bdfecd973af1b`. Its two dirty tracked files are VSCode
extensions and `include/WriterDeckBleUtils.h`; neither defines display refresh.
The installed dependency source is authoritative too: M5GFX metadata version
0.2.28, `Panel_SSD1677.cpp` SHA256
`c87a4c6a95a24d50d4d2750fe99215c655d673b040337523e3ace13fba093f58`.
Do not substitute a current upstream library or assume the version identifies
these exact dependency bytes. Application source `WriterDeckPaperApp.cpp` SHA256
`40525be3fea718fedc6446ec8b10ecfc84e2d3864efb8de93c346eb0cd98c94d`.

Both implementations use the same PaperMono ESP32-S3 / SSD1677 800x480 panel.
WriterDeck uses Arduino / espressif32 6.12.0, M5Unified, M5Canvas and the actual
`Panel_SSD1677_4Gray` selected in installed `M5GFX.cpp:2071`; not the base mono
driver or the other project's IT8951/Inkplate paths. Typing selects `epd_fastest`.

Crucial finding: `_send_transition_planes()` writes **both complete 48000-byte
RAM planes**, even for a tiny dirty rectangle. At steady-state fastest, 0x24 is
new mono within dirty and old mono outside; 0x26 classifies old-to-new transitions.
For SolarOS's strictly mono history this simplifies to new and completed-old.
The LUT holds unchanged pixels with zero drive and drives black/white transitions.
This is **C + D (E)**: a custom short waveform and application line/burst/repair UX.
There is no shortened gate window or reduced RAM payload in this fastest path;
neither A nor B is demonstrated. A two-line repaint is normally two independent
800x30 canvas pushes, potentially two activations and 192000 transferred bytes.

The custom fastest LUT has a 2-frame reverse prepulse and 8-frame dose, uses
0x32 plus gate/source/VCOM settings, and activates Mode 2 with 0x22=0xCC cold /
0x0C warm, 0x21=0, 0x20. No per-update software/hardware reset or deep sleep in
the healthy steady state. Comparison keeps analog power on after fastest.
Its BUSY wait explicitly bypasses the base 400ms minimum and guards delayed BUSY
assertion. Actual timings, ghosting and temperature/lifetime limits are unmeasured
at this review point; code comments do not establish long-term panel safety.

SolarOS currently uses OTP 0xFF, one 48000-B current-plane transfer, a hardware
wake/reset, 100ms readiness guard, and mode-1 sleep plus 100ms post-wait each
update. Recorded baseline physical fast is about 870–880ms (activation ~560ms,
conversion/SPI ~80ms). Rectangular transfer alone could at most remove most of
that ~80ms; comparison waveform/lifecycle changes can plausibly remove much more,
but this must be measured. Doubling payload is a real cost, not a byte-saving.

Prototype: preserve original OTP full/fast/auto/cleanup, add explicit diagnostic
reference mode in SSD1677 backend only. Establish a visible OTP full baseline,
write full current/completed-old planes, use the exact reference fastest LUT,
wait for BUSY with a bounded guard, and promote shadow only on total success.
Keep existing failure latch and force OTP full on loss of baseline or incompatible
mode/power transition. No Writer hardware branch, checkpoint/BLE/key/caret/layout
changes. Start with full white, rectangles, erase and repeated transitions; measure
RAM conversion/SPI, BUSY, total, bytes separately. Do not activate a production
default until latency and user-observed image quality justify it.

If successful, keep immutable snapshots and latest-pending executor. Any difference
is from the **completed** shadow, never dropped submitted frames. One bounding box
is sufficient for diagnostics/history; the reference waveform itself still uses
full RAM writes. A rectangular RAM optimization is a separate hypothesis because
stale transition classes outside the window can re-drive previous changes.
Comparison cleanup=128 application-counted partial calls plus 30s idle and no
pending repair, not a controller/library limit. Copying that number as a safety
guarantee would be unjustified.

## Code comparison

| Item | WriterDeck PaperMono | SolarOS baseline |
|---|---|---|
| Framework | PlatformIO espressif32 6.12.0 / Arduino; M5Unified + M5GFX 0.2.28 installed source | ESP-IDF 5.5.5; U8g2 + native SSD1677 |
| SPI | 40MHz configured | 10MHz board binding |
| Render buffer | 1-bit full screen canvas + 800x30 line canvas; M5GFX two 48000-B planes and two-plane displayed history | 48000-B vertical tile buffer, owned immutable async snapshots, completed 48000-B shadow |
| Input burst | Up to 24 queued printable chars applied as one burst; newline separate | Normal Writer events, full render then immutable latest-frame coalescing |
| Dirty generation | Render old/new cursor lines; cached line text **and cursor column** for delayed repair | Completed-shadow comparison; no Writer device branch |
| Two lines | Old and new cursor lines when different; not every key. Deferred repair max 2 calls/tick, after 300ms; 120ms between repair ticks | All bytes currently transmitted even for one changed glyph |
| Cursor | 3x24 bar inside 30-pixel line canvas, moved with line repaint | Existing Writer caret; no new blink policy |
| Wire bytes per line call | 48000 BW + 48000 RED = 96000; two calls=192000 | Fast 48000; full 3x48000=144000 |
| Nominal line canvas | 800x30/8=3000 B; two=6000 B, **not controller transfer size** | Snapshot=48000 B |
| RAM X/Y | Full 0..799, gates 479..0 for fastest transition planes | Full 0..799, gates 479..0 |
| Address commands | 0x11=01; 44=00001f03,45=df010000,4e=0000,4f=df01, repeated per plane | Same scan order/window, once for fast's plane |
| 0x24 / 0x26 | Explicit transition planes on every fastest call; outside dirty hold old | Fast current only; OTP manages previous baseline; full writes inverted current then both current |
| 0x21 | 00 | 00 |
| 0x22 / 0x20 | Fastest CC cold / 0C warm + 20; quality C7 cold / 07 powered + 20 | Fast FF +20; full F8 +20 then14 +20 |
| LUT | Fastest 105 bytes to32 + voltage bytes03/04/2c; 10 frames, high frame-rate code8f | OTP; no host LUT |
| Reset | Initialization/unknown RAM-face recovery; no reset each healthy fastest call | Hardware reset each fast; software reset for full/invalid baseline |
| Border / booster | Initialization3c=01,0c=aec7c3c040 | Full3c=01, fast3c=80; booster aec7c3c080 |
| After update | Fastest leaves analog on; application idle stages adjust frontlight/loop delay | Deep sleep10=01, rail retained,100ms post-wait |
| BUSY | 2ms assertion guard, poll1ms; no400ms floor for4Gray activation | 100ms guard,poll20ms; additional wake-ready wait |
| History | Updated after successful BUSY completion; mode2 face tracked, fixed plane addresses after overwriting both | Updated only after successful complete driver transaction |
| Cleanup | Application count>=128 AND30s idle AND repairs complete; also explicit quality refresh | Explicit cleanup; healthy unknown baseline full; hardware fault latches |

Thus a one-line UI write superficially saves 93.75% of canvas area (two lines
87.5%), but saves **0%** of fastest controller plane bytes. Relative to SolarOS
OTP fast it sends twice as many image bytes. The 40MHz wire floor for 96000 B is
19.2ms versus SolarOS10MHz/48000 B=38.4ms; actual calls include per-row overhead.
Prototype holds SolarOS at10MHz to isolate waveform/lifecycle changes.

Source navigation (relative to the comparison repository):
`src/WriterDeckPaperApp.cpp:1049` selects fastest for a line repaint;
`:1081` counts application partial calls; `:1680` performs deferred line repair.
Installed `M5GFX/src/lgfx/v1/panel/Panel_SSD1677.cpp:147` defines the LUT,
`:591` handles BUSY guarding, `:758` writes complete transition planes,
`:963` chooses transition planes/LUT, and `:1005` dispatches fastest.
These are the installed dependency paths under `.pio/libdeps/m5stack-papermono/`.

Reference fastest physically drives transition groups, while unchanged classes
hold. Full source/gate scan and both full RAM writes remain; no evidence that a
smaller dirty rectangle itself shortens BUSY. Temperature, lifespan and long-term
DC balance cannot be established from this code or a short room-temperature test.
No extra voltages or shortened LUT phases are invented for this prototype.

## Initial validation

Existing plus diagnostic SPI-capture host tests pass: unchanged skip, OTP full
baseline, full current/old RAM bytes, exact105-byte LUT, coldCC/warm0C, no warm
reset/sleep, failed LUT upload cannot advance shadow, explicit return to OTP
invalidates baseline. Normal operation remains AUTO/OTP.

Same-device pre-change `.research/hardware/strategy-baseline.log`: four fast
rectangle/erase transitions around872ms; BUSY about559ms; convert about30ms and
SPI about48ms. These are driver timestamps, not video-based pixel settling.
## Prototype measurements

Same physical PaperMono, same 10MHz SPI, diagnostic present, timing enabled.
The comparison application itself was not flashed: these measurements reproduce
its installed fastest LUT/transition-plane strategy in SolarOS, not its Arduino
render/transport overhead. BUSY completion is a physical-driver proxy, not an
optical sensor measurement of the last pigment settling.

| Path | Total | Activation/BUSY | Image RAM bytes |
|---|---:|---:|---:|
| Original OTP full white | 4518ms | 100ms + 3819ms | 144000 |
| Original OTP fast, four rectangle/erase updates | 872ms | 559ms | 48000 |
| First custom waveform after OTP baseline (cold) | 587ms | 134ms | 96000 |
| Warm custom waveform, rectangle/erase | about245ms | about52ms | 96000 |

Warm updates spend about159ms on two writes, including about61ms tile conversion
and96ms SPI polling. Completed-shadow pixel comparison and command overhead account
for most of the remainder. The measured improvement is about72% in driver total,
despite doubling image bytes. Tiny native dirty bounds are56x30; the RAM window
remains800x480 for each plane. Cold initialization is reported separately.

Evidence: `.research/hardware/strategy-baseline.log`, `strategy-first.log`,
`strategy-repeat20.log`, `strategy-repeat51.log`. No cleanup was performed between
these trials. The user confirmed the first black rectangle was sufficiently dark,
and confirmed no residual rectangles, white background dirt or edge artifacts
after21 and51 custom activations. At101 activations, including fine text/lines,
corners and half-black patterns, the user reported faint residuals that were not
bothersome. This is a quality tradeoff, not a ghost-free result. The subsequent
typing trials are reported below.

All201 custom activations completed without error. The last100 had driver total
median244.578ms (243.516–277.231ms), BUSY median51.390ms (50.980–51.892ms).
The final full-black-to-white transition took276.209ms with51.415ms BUSY. Fine
lines, corners, half/full black therefore did not shorten/lengthen BUSY in
proportion to dirty area; this supports the full-scan short-LUT interpretation.
The initial cold activation is excluded from these warm statistics. At201, the
user confirmed no visible residuals. This does not erase the faint residual
observation at101: the patterns subsequently included full black/white, so this
is not a monotonic ghosting test. An explicit OTP cleanup subsequently completed
in4520.386ms (activation99.849+3819.876ms), with no driver error.

## Integration decision

The measured ~72% warm-driver improvement warrants integration as opt-in
`refresh=fastest`, using the tested backend through the existing async executor.
The prototype spelling `refresh=reference` remains an alias for reproducible logs.
AUTO/partial retain the OTP waveform; the mode is not persisted. This preserves
the existing power/ghosting behavior across boots while making the faster path
usable by Writer and other framebuffer clients without application changes.
No automatic128-update cleanup is copied: observed residuals vary with content,
and this run does not establish a universal safe count. Explicit cleanup remains
available; invalid baseline, mode crossing and rail-off force an OTP full update.
The first custom update after a full baseline is cold; subsequent updates are warm.
Host shadow advances only after successful writes and BUSY completion. Failures
latch the driver/executor and require recovery/reboot instead of uncontrolled retries.

Both normal and bringup builds pass. The normal application flashed to COM39 is
`build/papermono-strategy-integrated/firmware.bin`, SHA256
`0e51c83b95ad298b2c4bf7f5532619ffbc5e7b9af87b1f896e98f8b5a1adc3ce`.
Only the application partition was written. The bringup build is saved separately,
SHA256 `c95bb90e43a98b8526a823d278dc5fb0dcfead62aabca79f01ea476230a30eda`.
The user confirmed the white screen was normal after the explicit cleanup.

## Writer measurements

The same integrated firmware provides both policies. Both comparison documents
start with a synthetic `.` (excluded from BLE measurements), then ten isolated
BLE `a` inputs. Driver diagnostics and pipeline logging are enabled on both sides.
With OTP partial, all10 reports matched Writer inputs, renders and physical
completions; no replacements or full fallbacks occurred. BLE report to worker
completion median915.424ms, range885.264–937.829ms. Worker duration median880.578ms.
The user confirmed no missing characters or display artifacts. Evidence:
`.research/hardware/strategy-before-isolated.log` and generated comparison JSON.
Subsequent before trials: `abcde`,20 digits, then Enter followed by the44-character
sentence `The quick brown fox jumps over the lazy dog.`. The Enter was a user
variation and must also precede the after sentence for the comparison.

Fastest isolated trial: the first10 BLE `a` reports completed in median281.958ms,
range256.881–313.904ms (about69.2% lower median than OTP). A further isolated `a`
was recorded25s later, before the burst trial; all11 were successful. Report
the first10 for matching positions; retain the extra observation in the raw JSON
(all11 median284.999ms). The user reported clearly faster text and no display
errors, with slightly lighter text that was not bothersome. Typical native dirty
bounds were16x16 (one8x16); each activation still sent96000B and BUSY was about52ms.
The after burst document consequently has one extra `a`; later manual long-text
and Enter repetitions also have reported human variation. These are functional
and practical latency comparisons, not identical keystroke-timestamp replays.

Python integration/analysis suite initially:546 tests,4 skips, passed in WSL. An initial
Windows attempt lacked `cc`/`make` and used CP932 for default text reads; those
environment failures were resolved by running the suite in the established WSL
toolchain, without changing product code to accommodate the test environment.

### Operation comparison

Tail latency is the **last relevant BLE report to driver completion**, not a
camera measurement. Every listed typing/edit/scroll trial had zero OTP full
fallbacks. Full updates for mode changes and resume are counted separately.
Transfer counts below are per operation sequence, not per key. Input cadence is
human-controlled; more completed intermediate frames in fastest increase bytes.

| Operation | OTP tail | Fastest tail | Physical updates OTP / fastest | Image bytes OTP / fastest |
|---|---:|---:|---:|---:|
| A: isolated `a`, matching first10 | median915.424ms | median281.958ms | 10 / 10 | 480000 / 960000 |
| B: `abcde` | 1441.726ms | 322.750ms | 3 / 5 | 144000 / 480000 |
| C:20 digits | 1523.456ms | 390.302ms | 6 / 13 | 288000 / 1248000 |
| D: sentence (see variations) | 984.831ms | 284.895ms | 11 / 40 | 528000 / 3840000 |
| E:90 digits crossing a line boundary | 1087.605ms | 382.364ms | 20 / 67 | 960000 / 6432000 |
| F/G: trailing Enter / Backspace | no physical update | no physical update | 0 / 0 | 0 / 0 |
| H: left arrow | 936.351ms | 285.724ms | 1 / 1 | 48000 / 96000 |
| H: right arrow | 940.166ms | 282.011ms | 1 / 1 | 48000 / 96000 |
| I:29 Enters then `END`, excluding save | 1585.831ms | 483.528ms | 12 / 23 | 576000 / 2208000 |

OTP BUSY was approximately560ms per activation throughout. Fastest BUSY medians:
B51.278ms, C51.433ms, D51.505ms, E51.509ms, I51.441ms; driver totals typically251–253ms.
RAM window is always800x480. Fastest native dirty bounds: A/B mostly16x16;
C includes200x474 when status/text changes are distant; D up to200x32;
E wrap transition792x32; H16x16; I includes664x467 during scrolling. These tall
unions are **difference bounds**, not extra RAM traffic or a physical scan window.
OTP baseline logs did not expose dirty bounds; its transferred window is full frame.
The raw per-update region lists and times are in `strategy-writer-results.json`.

Variations: after C was initially only10 digits (425.433ms tail), so a separate
complete20-digit trial was collected later. Before E contains a digit-order typo;
both E trials contain90 key inputs. After D includes four Backspaces correcting
two typing mistakes, giving53 events versus45 before; final sentence is the same.
Both scrolling trials actually contain29 Enters, followed by `END`; save latency
is excluded above. These variations limit direct statistical comparisons beyond A.
The user confirmed faster appearance, successful wrapping/cursor/scrolling and
no missing characters or objectionable residuals. Slightly lighter text was
reported for fastest. The unchanged trailing-newline caret limitation remains:
Enter/Backspace can change document bytes with identical rendered pixels, so
the correctly skipped physical update is **not** a near-zero visible response.

Two after-trial records were interleaved by concurrent native and foreground USB
logging: one WORKER_START lost its `t` field, one SNAPSHOT_SUBMIT lost `replaced`.
The analyzer now reports `malformed_events`, skips those records and never invents
times/links. Both have intact separate completion records; driver records provide
the operation counts above. A/B/C have no malformed records. Keep this limitation
with the raw logs; aggregate worker/submission counts alone omit those records.
The new parser test verifies both missing-start and missing-end behavior.

### Suspend regression and correction

Initial fastest regression passed KEY2 newline plus text, original-document
retention, resume typing and Ctrl+S. Logs show KEY1 rail-off, then an OTP full
resume4526.634ms and cold fastest595.896ms on the first new text. However, the
user observed pronounced grain after suspend, cleared by resume's full update.
Inspection found a missing lifecycle step: comparison
`Panel_SSD1677.cpp:448` issues22=03 (analog/clock off),20, BUSY wait, **then**10=01.
SolarOS previously went directly to deep sleep/reset/rail-off with fastest analog
power retained. The sleep path now uses that reference sequence when fastest is
awake; normal typing remains unchanged. On command/BUSY failure it invalidates
the baseline, latches a fault, and does not proceed to reset/rail removal.
Host command-order and failure tests pass. Corrected hardware power-off completed
successfully in160.094ms before deep sleep/reset/rail-off. The user initially
reported no grain, then corrected that observation: much improved, but a little
grain remains. The final acceptance was that it is unobjectionable during typing
when later refreshing is expected. Do not report the grain as completely fixed.
The short-LUT path is therefore retained as an explicit quality/speed option.

Final normal firmware: `build/papermono-strategy-poweroff/firmware.bin`, SHA256
`7a4f68aa61fd79d8ff906ae8ca791fcdc3cbe2e17df97de5df218f8eb40f2cbc`, flashed
app-only and verified. Final bringup firmware:
`build/papermono-strategy-poweroff-bringup/firmware.bin`, SHA256
`00a857eb66fe31b2e265a06a1aac3ada2ac24bcde720c1735bf43ccdc00220f8`.
Normal static RAM52464B, flash3316683B; bringup RAM52336B, flash3312519B.
The typing-path timing comparison predates only the sleep correction. The final
build's warm cursor update measured247.118ms, BUSY51.876ms, and the correction
adds no work to active typing. Saved before/after documents were read back before
reflashing and the after document reopened successfully.

Final corrected hardware closeout: KEY1 resumed through an OTP full update of
4527.213ms; BLE `xyz` input and Ctrl+S completed successfully. The user completed
the requested four-corner touch sequence and final confirmation. `input test
touch0` reports8 pointer events (four presses/releases), source ready and final
buttons released. Only the final pointer coordinate was queried; this is a
touch-reception regression, not a new four-corner calibration measurement.
Saved `/strategy-after.md` was read back ending in `kRESUMExyz`; the before
document and earlier checkpoint documents were preserved. Final runtime is
Writer active, BLE connected, `refresh=fastest`, timing off, log level info,
CDC log sink off; COM39 released. Boot still defaults to AUTO/OTP.

## Completion checklist

| Requested item | Result |
|---|---|
| 1. Validity review | Recorded before implementation above; prototype first, integration gated by measurements and visible quality. |
| 2. Revision/path | User workspace and exact root revision/dependency hashes above; comparison workspace unchanged. |
| 3. Framework/library | Comparison Arduino/espressif32/M5Unified/M5GFX; SolarOS ESP-IDF/U8g2/native SSD1677. |
| 4. Display stack | Installed4Gray SSD1677 fastest path traced through registers, not inferred from the M5GFX name. |
| 5. Framebuffer | Reference line/full canvases plus two-plane history; SolarOS mono snapshots plus completed shadow. |
| 6. Dirty tracking | Reference cached line text/cursor plus delayed repair; SolarOS completed-frame difference. |
| 7. Typical two lines | Reference up to two800x30 pushes; SolarOS observed wrap dirty792x32 in a single presentation. |
| 8. Transfer bytes | Reference96000 per push,192000 for two; SolarOS fastest96000 per activation. No rectangle byte saving. |
| 9. Commands | Register/lifecycle comparison table above, with corrected analog-off sleep sequence. |
| 10. Previous/current | Strict mono transition specialization:0x24=new,0x26=completed-old, full planes every time. |
| 11. Activation |22=CC cold /0C warm,21=00,20; original OTP retained. |
| 12. BUSY | Warm~51–52ms versus~559–560ms OTP. |
| 13. Full timing | OTP full~4.52s,144000B, two activations~100ms +3.82s. |
| 14. Typing update | Actual wrap792x32:251.520ms driver,51.205ms BUSY,96000B. |
| 15. Physical rectangle | Not demonstrated; unchanged transition classes hold but gate/RAM scan remains full. Classification C+D/E. |
| 16. Cleanup | Explicit OTP cleanup, forced full on invalid baseline; no copied128 threshold. |
| 17. Ghosting | None at21/51, faint at101, none at201 after additional black/white patterns; slightly lighter Writer text. |
| 18. Difference | Short custom LUT and warm power lifecycle dominate; source application batching is secondary. |
| 19. Portability | Worth integrating on this PaperMono; no claim for other controllers or temperature range. |
| 20. Architecture | Existing Writer→immutable snapshot→async worker→completed shadow→PaperMono backend. |
| 21. Prototype |201 successful custom activations; user observations and logs retained. |
| 22. Region/bytes/time | Tiny56x30~245ms; full96000B and~52ms BUSY in all warm cases. Cold~587ms. |
| 23. Production | Selectable fastest backend integrated; AUTO/partial keep OTP. |
| 24. Region generation | Existing vertical-tile XOR pixel comparison, byte-aligned X, exact Y, one bounding rectangle. No new region list. |
| 25. Async worker | Existing one active/one latest pending policy retained; no Writer hardware branches. |
| 26. Shadow | Promoted only after complete successful writes/BUSY; dropped C cannot become B→D baseline. Host-tested. |
| 27. Baseline/fallback | Boot/rail-off/mode crossing use OTP full; faults latch instead of repeated unknown-state activations. |
| 28. Isolated before/after | First10 median915.424→281.958ms; no full fallback. |
| 29. Burst before/after |5-char tail1441.726→322.750ms;20-char tail1523.456→390.302ms. Human variations documented. |
| 30. Edits/scroll | User-confirmed; trailing Enter caret issue unchanged. Per-key cursor and scrolling metrics above. |
| 31. Regression | Host driver/async/checkpoint/files/gfx/keys/HID/touch/power pass; Python547 tests/4 skips pass; both final builds pass; diagnostic/Writer, keys, resume/full fallback, BLE, Ctrl+S, touch and USB shell verified. Mild suspend grain remains as accepted above. |
| 32. Unresolved | Temperature/lifetime/DC balance and retained-power consumption unmeasured; faint residuals/lightness and mild suspend grain remain; existing EOF caret, BLE intermittent reconnect and large-document layout unchanged. |
| 33. Next improvement | Optimize full-plane tile conversion/transport and evaluate40MHz SPI independently. About159ms of warm update is two writes; current waveform should not be shortened further merely for a target number. |

No rectangular-RAM optimization is included: writing only a dirty window can leave
stale transition classes elsewhere in the two planes. Multiple rectangles would
not reduce the present full-plane payload or BUSY; one bounding box is sufficient.
No dirty-only Writer rendering, speculative text, extra cursor refresh or source
application repair scheduling was copied. Existing coalescing already updates
the newest immutable image without blocking input on the physical refresh.

## Use

For the current boot, choose the faster path through the normal display control:

```text
display mode display0 refresh=fastest
```

The next presentation establishes an OTP full baseline if needed. Return to the
original policy with `refresh=auto` (or`refresh=partial`); request one full cleanup
on the next presentation with `refresh=cleanup`. An idle Writer may need a cursor
move or another normal render before that queued cleanup occurs. Boot defaults
remain OTP, and these settings are not persisted. Driver timings are available
with `display mode display0 timing=on`; pipeline timings additionally need debug
logging. Normal use should leave verbose timing/debug off.

Host validation also covers individual corner/boundary pixels, a vertical
framebuffer byte, full-width one-row change, distant corners, identical-frame
cleanup, cold restart after cleanup, rail-off/full fallback, and BUSY timeout
without advancing the completed shadow or retrying a faulted panel. The existing
async executor test passes immutable snapshots,1000 pending replacements,
controls, suspend, shutdown, faults and allocation failures.
