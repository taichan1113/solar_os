# PaperMono text contrast experiment

Branch: `experiment/papermono-text-contrast`.
Baseline: `2935fd94` (platform services, terminal, Writer and BLE work saved on
`main`). This experiment does not change Writer editing, BLE security, fonts,
the fastest LUT, transition encoding or panel voltages.

## Rejected absolute-drive trial

Commit `3089aaba` added `refresh=balanced`, using the official M5GFX fast
absolute monochrome waveform on every changed frame. At 40 MHz, warm driver
median was 315.06 ms (BUSY 249.79 ms), against fastest 113.95 ms (BUSY 51.68 ms).
The user rejected the visible flicker as unacceptable. Commit `e684d8b9`
reverts that implementation; balanced is absent from this firmware. Runtime
was returned to fastest before proceeding. The initial report's contrast
benefit was not visually established and is not an accepted result.

## PaperMono reference comparison

`E:\ESP32\M5Paper_Writerdeck\src\main_papermono.cpp` uses
`WriterDeckPaperApp.cpp`; the legacy `WriterDeckM5PaperDisplay.cpp` is a different
device path. Relevant PaperMono behavior:

* `setFastMode()` selects `epd_fastest`.
* The 300 ms repair deadline fills unrendered lines with the same fastest
  waveform. It is not a separate darker waveform.
* The actual PaperMono build defines `PAPERMONO_PARTIAL_BUDGET=128`
  (`platformio.ini:172`); the source-only fallback of 8 is not the build setting.
* Once the update budget is reached, quality refresh waits for both input and
  interaction to stop for 30 seconds (`WriterDeckPaperApp.cpp:4886`).

40 MHz is the SPI transfer clock, not a 40 Hz display refresh rate. Faster SPI
does not by itself lengthen or shorten the LUT waveform. Earlier 10/20/40 MHz
pattern tests did not show corruption; 20 MHz did not improve suspend grain.
Accumulated partial refreshes and the shorter interval between SolarOS updates
are plausible contributors to fading. These observations do not establish a
single physical cause or guarantee that pacing alone fixes contrast.

## Opt-in typing policy

```text
display mode display0 refresh=typing
```

Typing uses the existing fastest waveform and 40 MHz setting. It adds two
scheduling rules:

1. Physical update starts are spaced at least 250 ms apart. Input continues in
   the application task; the pending snapshot is replaced by the newest complete
   image. Intermediate images may be skipped, but document input is retained.
2. After 128 physical updates and 30 seconds with no new framebuffer submission,
   the worker performs one OTP full cleanup of the last completed snapshot.
   Pending input takes priority, and new submissions postpone the idle deadline.

250 ms pacing is an experiment based on the reference's measured update cadence,
not a reference source constant. The idle criterion is conservatively based on
framebuffer submissions, rather than a Writer-specific keyboard/touch timer.
Cursor renders can consequently postpone cleanup. Continuous typing never
triggers a budget cleanup on its own. Fading during very long uninterrupted
typing may still require manual cleanup.

If input resumes just after the full cleanup starts, display completion can wait
about 4.5 seconds. The application still accepts input, and its newest snapshot
is displayed afterward. The first fastest update after OTP cleanup is cold and
can take longer than a warm update. No claim of flicker-free *idle full cleanup*
is made; the rejected per-keystroke absolute waveform is not used.

Runtime tuning (one option per command):

```text
display mode display0 typing-frame-ms=250
display mode display0 typing-cleanup-updates=128
display mode display0 typing-idle-ms=30000
display async display0
```

Frame interval accepts 0–1000 ms; cleanup budget 0–4096 (0 disables automatic
cleanup); idle delay 30000–300000 ms. Changing a policy resets its accumulated
budget. The async status exposes separate maintenance start/completion counters;
maintenance does not invent submission generations. The executor uses timed
notifications and the existing two immutable PSRAM snapshots, with no polling
loop or additional framebuffer allocation. Controls join any in-flight update;
suspend, direct presentation and shutdown invalidate the retained cleanup image.
Faults latch and cancel pending work as before. Typing selection is rejected if
the async executor is unavailable.

Return to the original behavior:

```text
display mode display0 refresh=fastest
```

Fastest↔typing shares its baseline and does not force an extra full refresh.
Manual `refresh=cleanup` still queues full cleanup for the next presentation.
The saved boot startup remains `refresh=fastest`; the experimental policy and
tuning are not persisted.

## Validation and artifact

Host tests pass for SSD1677 (unchanged fastest LUT and mode baseline), real
executor pacing/coalescing, delayed cleanup, immutable maintenance snapshots
under concurrent input, idle deadline postponement, cancellation while paced,
suspend, controls, shutdown, allocation failures and fault propagation.
Driver and executor tests also pass ASan/UBSan. Writer checkpoint, BLE reconnect,
and all three terminal rendering preset regressions pass.

Normal PaperMono firmware build succeeds. Static RAM: 52904 bytes (unchanged).
Flash used: 3336987 bytes (+2704 from baseline). App binary: 3342768 bytes.

* Artifact: `build/papermono-typing/firmware.bin`
* SHA256: `48ea3340b617c55ea4888af3af4cde93fb596f428fd781d36976cc31e1f496fc`
* ELF SHA256: `bc9af897ad1d37c8e06a4c5ed0f899b9aa4e1ec98523a800368dc89b9405ec5d`

App-only flash at `0x10000` on confirmed COM39 passes hash verification. The
existing trial file was read back before flashing. Existing user
documents, startup, font/JP keyboard settings and BLE bonds were not overwritten.
ZMK reconnected automatically. Timing scripts operate on separate test documents;
invalid dialog/stale-prompt runs are excluded from measurements.

For 20 synthetically injected characters at approximately 100 ms intervals, the
physical counter increased by 6 in fastest and 5 in typing. Native warm timing
records agree with those counts. Median driver durations were 809.24 ms and
949.32 ms respectively; BUSY medians were 68.15 ms and 52.10 ms. Concurrent
rendering and USB logging substantially increased conversion/transfer wall time
in this run. These are not isolated-frame timings or BLE-to-visible latency,
and the small count difference does not establish a speed improvement.

With a temporary diagnostic budget of 2, one cleanup completed 30 seconds after
the last submission (OTP driver 4354.60 ms). Submission generation remained 118
while the physical count increased by one, proving that cleanup was separate
from accepted input. Further `POST` input reached generation 122 and completed
without a latched error. The default budget was restored to 128 and timing/log
output disabled afterward.

The USB virtual `input emit CTRL+S` command retains a letter and modifiers,
whereas Writer saves on the translated character 0x13. It therefore does not
prove a save from this test helper; the new document's actual ZMK Ctrl+S and
subsequent file readback were confirmed during manual acceptance. This input-tool limitation
does not change the production HID translator.

## Panel observation

The user completed the new typing test on ZMK and reported:

* No visible flicker during input.
* Initial text still became slightly lighter, subjectively about 80% of its
  initial darkness, but remained readable. This is an impression, not an optical
  density measurement. Fading persists; its elimination or improvement over a
  matched fastest typing trial has not been established.
* Input-time waiting was almost unnoticeable.

Readback confirms the diagnostic 40 digits, `UV` and `POST` in their original
order in `/contrast-typing.md`, followed by the user's test text. The actual ZMK
save therefore succeeds. Runtime readback confirms JP keyboard, mono font,
unchanged fastest boot startup, and connected remembered keyboard 1/1. After the
user's typing and pause, maintenance completion advanced from 1 to 2 with no
error or pending work: one further full cleanup happened at the normal budget
of 128. There was no need to reset or clear BLE bonds.

The user separately confirmed that the initial text's darkness returned after
the idle full cleanup. Typing remains opt-in on this experiment branch, and boot
continues to select fastest. Host tests cannot establish optical quality.
