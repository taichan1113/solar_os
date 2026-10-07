# PaperMono asynchronous display execution

## Task validity review — before implementation, 2026-10-07

The request is justified by measured reception-to-dispatch delays (short burst
median 695 ms, long burst 758 ms) and the synchronous call chain in
`doc/m5paper-mono-pipeline.md`. Rendering is already 2–4 ms. A checkpoint still
blocks occasionally, but cannot explain the repeated ~0.87 s display stalls.
Moving checkpoint I/O first would leave the dominant continuous-typing stall.

The existing `solar_os_display_present()` is a synchronous operation, although
its void result does not propagate a controller error. All call sites were
enumerated in `.research/async-callsites.txt`: normal app rendering, snapshot
restore, splash, terminal/layout forwarding, and claimed CLI display tests.
Legacy callers, especially diagnostics, restoration and layout forwarding, must
retain this completion boundary. A global semantic change is inappropriate.

| Alternative | Assessment |
| --- | --- |
| A: all targets use a generic worker | Unnecessary risk to LCD, raster/INDEX8, virtual layouts and other E-paper targets. Rejected. |
| B: optional async capability | Selected: only an explicitly enabled backend supplies submission, wait and status operations. |
| C: invisible SSD1677 worker behind SendBuffer | Would silently change all synchronous callers and obscure error/completion semantics. Rejected in that form. |
| D: explicit submit and wait alongside present | Selected with B. Writer uses generic submission; existing present remains synchronous. |
| E: shorter tick or dirty RAM rectangles | Cannot remove the 560 ms activation wait or the ~0.87 s blocked caller. Insufficient for this objective. |

Display targets own their U8g2 buffers. Claims are reference-counted per owner;
they govern application access, not asynchronous hardware lifetime. The display
service has a per-target present mutex and temporary lifetime references. Power
save and mode changes currently bypass that mutex. Therefore merely adding a
worker under the existing mutex would not serialize all SSD1677 operations.
Hardware serialization must also cover the driver's U8g2 callback, mode change,
resume and deinitialization paths.

Chosen design: a reusable latest-frame executor, an opt-in backend capability,
and explicit generic submit/wait/status APIs. PaperMono enables the executor;
other displays fall back to their existing synchronous presentation. Only Writer
opts into submission initially. The executor holds two fixed PSRAM snapshots
(48,000 bytes each), one in flight and one replaceable pending frame. It never
lets the worker read the mutable rendering buffer or U8g2 drawing state. Fixed
allocation avoids per-frame fragmentation and bounds lifetime/memory. The
existing internal/DMA row buffer remains the SPI source; PSRAM snapshots are not
passed directly to DMA. Startup allocation failure keeps synchronous operation
available and reports that async is unavailable.

Hardware controls form a barrier: stop worker promotion, join any in-flight
operation, then execute the control. Suspend/shutdown and synchronous replacement
discard pending frames. Mode/cleanup changes preserve the pending image and apply
the new policy before it can be promoted. Suspend keeps submission closed until
successful resume; resume retains the existing full-baseline fallback. Only the
successfully transferred immutable image may update SSD1677 shadow/baseline.
Worker failure latches an observable error, drops pending frames and rejects later
submits; it is available through status/wait and the next submit result.

Task configuration evidence: ESP-IDF main priority 1, pinned CPU0; HID event worker
priority 4 with no affinity; NimBLE host priority `configMAX_PRIORITIES - 4`.
Use priority 1, no affinity, and an internal stack for the display executor. It
does not outrank input/BLE; 1 ms FreeRTOS time slicing and two cores let main run
during conversion/SPI, while BUSY delays yield. Measure rather than claiming this
alone guarantees a particular latency. Internal stack avoids flash/cache-disabled
restrictions during the unchanged synchronous Writer checkpoint writes.

The preceding phase's remaining physical edit tests were not completed. Its
recorded character/burst baselines remain valid; this phase will test edits,
scrolling and suspend explicitly on the asynchronous implementation.

## API and execution contract

`solar_os_gfx_submit()` / `solar_os_display_submit()` copy a complete frame and
return its generation. Success means acceptance, not panel completion. The
caller can immediately redraw its original buffer. INDEX8, virtual and other
targets without the capability keep synchronous behavior. Existing `present()`
and U8g2 `SendBuffer()` callers remain synchronous. An outstanding deferred
overlay also uses the synchronous boundary. No worker is introduced for LCDs,
headless operation or non-PaperMono SSD1677 variants.

The target registry holds a temporary reference while invoking capability
callbacks. Application claims retain their existing ownership meaning. Device
detachment first unregisters the target (which rejects active claims/references),
then joins and destroys the executor before freeing driver buffers. The worker
owns its snapshot until completion; it never accesses U8g2 drawing state. Its
control and state mutexes are separate. The state mutex covers only bookkeeping
and the bounded snapshot copy; no panel wait occurs while holding it.

`wait(timeout_ms)` fences the generation current at entry. Replacement by a newer
generation can satisfy that fence. Cancellation without a later completed state
returns invalid-state; timeout and latched controller error are distinct results.
This is a latest-state fence, not proof that each intermediate generation was
displayed. `display async display0 [wait]` exposes counters, outstanding state,
generation IDs and the error; the optional CLI wait allows 70 seconds.

Mode and cleanup commands join in-flight work and synchronously apply controller
policy, then allow the latest pending frame to proceed. Requesting cleanup sets
the existing full-once flag; it does not itself manufacture a frame. Existing
display diagnostics retain their synchronous refresh completion behavior.
Power-save discards pending frames, joins in-flight work, sleeps the controller,
and closes submission. The board then removes the rail. Successful resume opens
submission with the existing invalid-baseline/full fallback. Synchronous presents
discard superseded pending work and join before accessing controller state.

Only a successfully executed snapshot updates the driver's shadow. Worker
failure preserves the driver's baseline invalidation/fault handling, latches an
executor error, cancels pending frames and rejects future submissions. Writer
polls completion status as well as checking submit errors. The USB status/wait
commands expose the same fault. Recovery requires device reinitialization; there
is no hidden retry or automatic clearing of a fault. Startup allocation/task
creation failure frees partial allocations and keeps the original synchronous
backend, with a warning.

## Instrumentation and analysis

Profiling remains opt-in (`log level debug`, `display mode display0 timing=on`).
Existing KEY_REPORT, WRITER_INPUT, MUTATION_END, render and SSD1677 phase events
remain. SNAPSHOT_SUBMIT records generation, copy duration and replaced generation;
WORKER_START marks pending promotion and execution start; WORKER_END records
execution completion, error and whether hardware refresh occurred. SSD1677 OTP
start/end timestamps are the physical transfer/BUSY boundary. All use
`esp_timer_get_time()` microseconds. Equal snapshots may complete without physical
refresh, so successful worker count and physical count are deliberately separate.

`scripts/analyze_display_async.py` analyzes one uninterrupted Writer/generation
epoch. It deduplicates ESP/CDC copies, pairs received keys with Writer dispatch,
and follows explicit replacement chains. It never assigns a cancelled frame to
an unrelated later completion. Incomplete captures and refresh failures have no
successful completion timestamp. Trials are separated by >4 seconds between
received keys; the physical protocol waits 8 seconds between trials. The output
retains individual input, mutation, render, submission and completion timestamps.
Driver completion is not an optical measurement; the user checks panel quality.

## Host validation

The new pthread harness executes the production executor, with concurrent
submitter, worker and control threads. It verifies immediate source-buffer
mutation, exactly one active plus one pending image, 1,001 accepted snapshots
with 999 pending replacements and only two executions, no per-submit allocation,
and release of all three owned allocations at shutdown. It also covers pending
promotion, wait timeout/completion, cleanup/mode barriers, suspend while busy,
resume/full-baseline behavior, shutdown while busy, failure latching, each
allocation failure and task-creation failure. The real SSD1677 driver test also
mutates the original render buffer during SPI and verifies that both transmitted
data and completed shadow retain the immutable snapshot.

Eleven relevant host executables pass: executor, SSD1677, display layout, mono
gfx, Writer buffer/files, document layout, input, BLE HID, PaperMono power and
FT6336. Python suite: 545 tests, 4 skipped, remaining tests passed under WSL.
Windows execution hit `_curses` import and temporary-file access errors; its
failed attempt is retained separately, not counted
as a successful run. Analyzer unit tests cover replacement chains, duplicate
logs, cancellation, faults, incomplete captures and synthetic unmatched input.

## Physical validation

The measured firmware is `build/papermono-async/firmware.bin`, SHA256
`5135a40567f57277f770abbcd9cd66fc3247adeef049cd799a136a12698321e0`.
Normal and bringup builds passed. The first boot reproduced the pre-existing
BLE HID-open timeout; reconnect succeeded at device time 138.386 s. Logs:
`.research/hardware/async-boot.log` and `async-connect.log`. BLE policy/code was
not changed. The measurements below establish the target input-dispatch
improvement separately from host correctness testing.

### A–C measured results

User confirmed normal display for five isolated characters, five valid `abcde`
trials and three 20-digit trials. One additional `abccde` trial was acknowledged
as a typing mistake and excluded from the five-character comparison. Every
received key in that extra trial was also dispatched; it is retained in the raw
capture. Logs: `async-single.log`, `async-short.log`, `async-long.log` under
`.research/hardware`. Reproducible aggregation: `.research/async_measurements.py`
and its output `async-measurements.json`.

| Metric (median unless count/range) | Isolated, 5 trials | abcde, 5 trials | 20 digits, 3 trials |
| --- | ---: | ---: | ---: |
| BLE reception → Writer dispatch | 6.869 ms | 5.852 ms | 5.800 ms |
| Dispatch range | 1.805–9.620 ms | 0.700–9.675 ms | 0.313–9.818 ms |
| Dispatch for keys received during refresh | — | 5.860 ms (20 keys) | 5.820 ms (57 keys) |
| Writer mutation | 0.101 ms | 0.102 ms | 0.106 ms |
| Render | 1.359 ms | 1.981 ms | 3.253 ms |
| Render end → submit return | 1.429 ms | 1.389 ms | 1.483 ms |
| Worker execution including driver/log overhead | 880.597 ms | 878.377 ms | 880.458 ms |
| Render / submit count | 5 / 5 | 25 / 25 | 60 / 60 |
| Replaced pending frames | 0 | 10 | 42 |
| Physical refresh count | 5 | 15 (3 each) | 18 (6 each) |
| First received key → final display completion | 923.345 ms | 2673.792 ms | 5298.132 ms |
| Last received key → final display completion | 923.345 ms | 1669.697 ms | 1693.164 ms |

The preceding report's 694.983/758.266 ms burst dispatch medians sampled the
**first input of each rendered batch**, not every key. Its asynchronous
counterparts here are 5.852/5.800 ms (each key produced a render). For a fair
all-key comparison, reprocessing the original raw captures with the same key
matcher gives **384.951 → 5.852 ms** for abcde and **482.469 → 5.800 ms** for
20 digits. All 77 keys received during ongoing refresh in these valid burst
trials were dispatched within 10 ms. This directly establishes that panel BUSY
no longer blocks normal Writer input dispatch.

Physical completion speed was not the optimization target. Isolated end-to-end
median changed from 898.768 to 923.345 ms. abcde first-key-to-final changed from
2671.066 to 2673.792 ms; 20-digit from 5343.818 to 5298.132 ms. Last-key-to-final
medians changed from 1627.790/1368.896 to 1669.697/1693.164 ms. Human typing cadence
and alignment within an in-flight update differ between runs; these figures do
not show a panel-speed improvement. Physical counts changed from 16 to 15 for
five abcde trials and 17 to 18 for three long trials. The latter increase is
consistent with timing, not an unbounded queue: 42 of 60 pending frames were
replaced, and the latest state caught up within 1.70 s of the final key.

The unchanged checkpoint still produces approximately 0.7-second main-task
stalls after idle input groups. This is now a separate remaining latency source;
the measured burst trials did not overlap a checkpoint. No asynchronous storage
change or dirty-rectangle implementation was added in this phase.

### D–E: prose, editing, scrolling and the remaining stall

`async-prose-edits.log` contains 83 received/dispatched keys, 81 renders/submits,
54 replacements, 27 completed worker jobs and 23 physical refreshes. Seventy
keys received during physical work had dispatch median 5.691 ms and maximum
9.660 ms. User confirmed arrow movement and repeated-Enter scrolling, but
reported no visible cursor movement for the isolated Enter/Backspace pair.

That pair was dispatched and rendered as generations 145/146, without pending
replacement. Both completed successfully with `physical=0`: the generated
images equalled the already completed shadow. The unchanged common document
layout reproduces the cause independently of display execution:
`.research/async-newline-probe.c` reports EOF `(21,0)` for both `END` and
`END\n`; `END\n\n` gives `(0,21)`, and `END\nX` gives `(7,16)`.
Thus a single trailing newline has no distinct EOF caret position in this
existing Markdown layout. This is recorded as an existing Writer limitation;
it is not reported as an editing visual pass and was not silently fixed outside
the display task's scope. Enter mutations and multi-line scrolling still work.

The final `END` arrived during checkpoint I/O, not during a refresh. Its three
dispatch delays were **697.773, 542.768 and 448.931 ms**. The covering tick was
`1037915218..1038654753`, with `recovery_us=739524`. All three keys were then
applied in order and included in generation 179. This is direct evidence that
checkpoint I/O is now the most valuable next input-latency optimization.
The worker cannot remove blocking filesystem writes performed by main.

### Files and boundaries

| Area | Files |
| --- | --- |
| Capability and executor | `src/services/solar_os_display_submission.h`, `solar_os_display_async.h/.c` |
| Generic submission and fallback | `src/services/solar_os_display.h/.c`, `src/services/solar_os_gfx.h/.c`, `src/board/solar_os_board_display.h` |
| PaperMono enablement and ownership | `src/services/solar_os_ssd1677.c`, `src/drivers/epd_ssd1677.h/.c`, `packages/solar_os_packages.toml` (`driver_ssd1677`) |
| Writer opt-in and error observation | `src/apps/solar_os_writer.c` |
| Diagnostic fence/status | `src/shell/solar_os_shell_commands.c` |
| Host coverage | `tests/host/display_async_test.c`, `tests/host/async_stubs/`, `tests/host/epd_ssd1677_test.c`, `tests/host/Makefile` |
| Measurement | `scripts/analyze_display_async.py`, `tests/test_display_async_analysis.py`, this document |

The existing dirty workspace also contains the preceding port/refresh/profiling
work. Those changes are preserved, and must not all be attributed to this phase.
The review, executor/ownership, backend integration, Writer opt-in, tests and
measurement records form the logical review boundaries; no commits were created.

### Suspend, final firmware and diagnostic regression

User confirmed KEY2 followed by a character creates the next line, KEY1 sleep
and KEY1 wake preserve the document, and BLE input after wake works. In
`async-buttons.log`, suspend occurred at 1511.974 s and resume at 1525.122 s.
The first resumed refresh was full, 4526.042 ms, error zero. Subsequent typing
used fast refresh. Real BLE Ctrl+S emitted key 19 and saved successfully;
`async-preserved.log` records the actual document including the sentence,
blank lines, `END`, and `kRESUME`. At the end of that measured epoch: 191 accepted,
112 replaced, 79 completed, 74 physical, zero cancellations/rejections/faults,
and no outstanding work. Counts include startup, supplemental/typo trials,
edits, resume and diagnostic input; they are not the A–C subset totals.

The final firmware only splits the long `display async` status output into
three shell printf calls (the shell truncated one long call). It does not change
the measured executor/driver/Writer behavior. Both final builds passed:

| Build | Static RAM | Flash | SHA256 of firmware.bin |
| --- | ---: | ---: | --- |
| normal `build/papermono-async-final` | 52,376 B | 3,308,703 B | `2dc8dfc3b7f545dbe56ad4a05bb87c7b0705e09724f68f616d973eacf99b9777` |
| bringup `build/papermono-async-final-bringup` | 52,248 B | 3,304,983 B | `ed5cc764de355d6eff170a8554207895b5be88828419601d68fd1027fb07924d` |

The final normal image was flashed to COM39 and verified by esptool. Application
partition only; no filesystem/NVS erase. The saved document was reopened.
`async-final-boot.log` shows automatic BLE reconnection at ESP log timestamp
11.255 s without a bond/profile change on this boot.

Synchronous CLI diagnostics on the final image completed in order:
`refresh=full`/fine pattern 4520.470 ms; `refresh=partial`/left box 871.720 ms;
`refresh=cleanup`/the **same** left box 4520.597 ms; `refresh=auto`/white 872.440 ms.
Each reported error zero and returned `display test: drew display0` after
physical completion. Thus cleanup is not suppressed by equality with shadow,
the full-once policy remains intact, and manual diagnostics keep synchronous
semantics. They do not increment async worker counts.

### Memory

The final boot measures allocations immediately around executor creation,
before application/BLE startup. This isolates the async increment better than
subtracting two unrelated runtime heap snapshots:

| Region | Free before executor | Free after executor | Incremental allocation |
| --- | ---: | ---: | ---: |
| Internal 8-bit RAM | 221,799 B | 214,971 B | 6,828 B (6.668 KiB) |
| PSRAM | 8,061,540 B | 7,965,276 B | 96,264 B (94.008 KiB) |

Payload is exactly two 48,000-byte PSRAM snapshots, 96,000 B total; measured
increments also include allocator/task bookkeeping. The task stack is 6,144 B
internal RAM. At that point the historical minimum free values were 182,940 B
internal and 7,965,276 B PSRAM; minima include earlier boot activity and are not
attributed solely to worker creation. After diagnostics, internal free/low were
84.0/44.2 KiB and PSRAM free/low 7.3/7.3 MiB. After the long Writer test epoch,
internal free/low were still 84.0/44.2 KiB; PSRAM free/low were 7.1/7.1 MiB with
Writer document/layout/session allocations present. These are shell-rounded
runtime measurements, distinct from the exact creation measurements.

The host leak run proves no per-submit executor allocation across 1,001 submits
and zero live owned allocations/mutexes after destruction. A 1,000-frame physical
panel endurance run was not performed. Peak outstanding is bounded/tested at two.

## Requested report index

| # | Result |
| --- | --- |
| 1. Task validity | Valid: repeated synchronous display stalls dominate burst dispatch; review recorded before implementation. |
| 2. Architecture | Optional backend capability plus explicit generic submit/wait/status; PaperMono-only worker, Writer-only opt-in. |
| 3. Alternatives | A–E compared in the review above. |
| 4. Selection reason | Preserves synchronous APIs and other targets while moving the expensive operation off main. |
| 5. Task/thread model | Main renders; priority-1 unpinned internal-stack worker executes; HID/NimBLE priorities unchanged. |
| 6. Framebuffer ownership | App retains its render buffer; executor owns copied immutable snapshots. |
| 7. Snapshot location/size | PSRAM, 2 × 48,000 B; existing internal DMA staging retained. |
| 8. In-flight/pending | At most one executing and one latest pending snapshot. |
| 9. Replacement | Only pending can be replaced; document mutations are never dropped. |
| 10. Shadow/baseline | Shadow receives only the successfully completed snapshot; full fallback and equality skip preserved. |
| 11. Suspend/resume | Stop promotion, discard pending, join active, sleep/rail-off; resume rebuilds full baseline. Hardware confirmed. |
| 12. Failure | Latched error, pending cancellation, rejected future submits; caller status/wait/Writer polling observe it. |
| 13. Allocation failure | Free partial allocations and retain synchronous backend; host-tested. |
| 14. Changed files | Listed by responsibility above; preceding phase's dirty changes preserved. |
| 15. Host tests | 11 relevant C executables passed; Python 545 tests with 4 skips passed under WSL. |
| 16. Builds | Final normal and bringup passed; hashes and sizes above; final normal flashed and verified. |
| 17. Memory | Exact executor increment: internal 6,828 B, PSRAM 96,264 B. Free/minimum values and leak test above. |
| 18. Isolated latency | Five valid trials, median report-to-completion 923.345 ms. |
| 19. abcde dispatch | Prior first-per-render metric 694.983 → 5.852 ms. Same all-key sampling: 384.951 → 5.852 ms. |
| 20. 20-digit dispatch | Prior first-per-render metric 758.266 → 5.800 ms. Same all-key sampling: 482.469 → 5.800 ms. |
| 21. Render count | A/B/C: 5 / 25 / 60. |
| 22. Submit count | A/B/C: 5 / 25 / 60. |
| 23. Replacement count | A/B/C: 0 / 10 / 42. |
| 24. Physical count | A/B/C: 5 / 15 / 18; no forced refresh-count target. |
| 25. Final completion | First-key medians A/B/C: 0.923 / 2.674 / 5.298 s. Last-key burst medians: 1.670 / 1.693 s. |
| 26. Checkpoint stall | Reproduced: 739.524 ms checkpoint caused up to 697.773 ms input delay outside display execution. |
| 27. Regressions | Full/fast/auto/cleanup, KEY1/KEY2, saved document, BLE input, arrows/scroll, shell, sync diagnostics and final four-corner press/release verified. The single trailing-newline caret limitation is recorded separately. |
| 28. Unresolved | Existing single trailing-newline caret behavior; synchronous checkpoint/save I/O; pre-existing intermittent BLE reconnect timeout. Not mixed into the async implementation. |
| 29. Next optimization | Move checkpoint I/O off main with safe document-version/lifetime semantics; separately fix the EOF caret layout. Dirty rectangles have lower value for the measured input-stall problem. |

Diagnostic note: USB `input emit CTRL+S` produced key 83 plus a modifier, while
Writer's legacy handler expects control byte 19. That attempted save inserted
`S`; it was removed immediately with Backspace. Actual BLE Ctrl+S saved the
document successfully. This pre-existing synthetic-input mismatch was logged,
not changed in this phase. It is excluded from latency trial sets.

## Final acceptance, 2026-10-07

User confirmed normal display and additional input on the final firmware,
completed the four-corner touch sequence, and saved with ZMK Ctrl+S.
`async-final-state.log` verifies the saved document still contains the preceding
text and ends in `kRESUMEXYZXYZ`. Two distinct XYZ input sequences are present in
the log, approximately 30 seconds apart; both were preserved. These final smoke
checks are excluded from the quiet A–C latency trial sets.

Final executor status: enabled, not busy, no pending frame, not suspended,
error zero; 8 accepted submissions, 2 replaced, 6 started/completed and 6 physical
updates, 0 cancelled/rejected; submitted and completed generation both 8.
Snapshot payload is 96,000 bytes and **peak outstanding is 2 on hardware**.
BLE remains connected with one remembered keyboard. Internal free/minimum heap
is 84.0/44.2 KiB; PSRAM free/minimum 7.1/7.1 MiB with Writer running.

`async-final-touch.log` contains four press/release pairs (pointer counter 0→8,
press buttons=1, release buttons=0). As in the preceding input phase, `input
test touch0` reports pre-orientation coordinates. `dispatch_input_pointer()`
applies the unchanged 270° orientation afterwards:

| Touched corner, in requested order | Measured source coordinates | Calculated landscape `(799-y,x)` |
| --- | --- | --- |
| Upper left | (111,698) | (101,111) |
| Upper right | (120,99) | (700,120) |
| Lower left | (393,698) | (101,393) |
| Lower right | (394,108) | (691,394) |

These confirm the four corresponding quadrants and the existing orientation;
the transformed values are calculated from raw events, not measured GUI hit-test
results. FT6336 code, calibration and orientation were not changed in this phase.

Final runtime settings were restored to `refresh=auto`, `timing=off`, log level
`info`, and CDC log sink off (`async-final-normal-settings.log`). Writer session
1 remains active with the saved document and BLE connected. The capture process
was closed, releasing COM39. Required builds/tests and `git diff --check` pass.
