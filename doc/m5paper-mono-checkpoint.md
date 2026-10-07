# Writer asynchronous checkpoint

## Task validity review (before implementation, 2026-10-07)

Proceed with asynchronous checkpoint I/O. The preceding physical trace records
739524 us inside recovery on the application tick and three received keys waiting
697773 / 542768 / 448931 us. Increasing the two-second idle threshold would only
move that stall; it would not prevent typing during a write from stalling.

Current flow: mutation sets dirty and clears recovery_written; the 40 ms tick,
after two seconds idle (and two-second failure backoff), flattens the gap buffer,
safe-replaces `.writer/<FNV-path>.recovery`, then separately safe-replaces `.meta`.
The document limit is 256 KiB. Flatten is a byte copy, not Markdown serialization.
The worker must never refer to the Writer state slot, gap buffer, document, layout,
or session context: the state slot changes when sessions are dispatched.
All text edits including undo/redo pass through writer_mark_changed; opening and
accepting recovery are separate version initialization boundaries.

The existing helper writes a temporary file, fflush/fsync/closes it, moves the old
destination to backup, installs and verifies the new file, and restores the backup
on failure. FatFs rename does not replace an existing destination. ESP-IDF VFS
locks individual operations, not a multi-call transaction. Keep the helper and
exclude competing Writer transactions on each path. Its rename sequence is not a
power-loss-atomic filesystem transaction; this task must not claim otherwise.

The existing separate metadata write ignores errors. Before making this concurrent,
bundle the recovery text and its cursor/scroll/zoom/path metadata in one versioned
record, committed by that same safe-replace helper. Continue reading legacy plain
recovery and metadata. A saved/discarded record invalidates old recovery durably;
explicit save must not report success if that invalidation fails.

Snapshot choices: immutable serialized UTF-8 plus small metadata is the smallest
ownership change. A deep document/layout snapshot copies unnecessary parsed state;
sharing a locked mutable gap buffer either blocks editing or adds lifetime races.
Choose the byte snapshot, measuring copy time for actual and maximum documents
before accepting its main-task cost. Copy timing remains **not yet measured** in
this phase; the 739524 us figure includes I/O and is not a serialization estimate.

Use one Writer-specific shared task, with per-session version/status and one latest
pending snapshot per session, plus one globally active write. Parked/detached
sessions exist, so an unbounded internal stack per Writer is unsuitable. A global
filesystem service or user-visible generic job registry would broaden this task.
Reuse SolarOS task creation/admission. Clients reserve their recovery paths while
alive; conflicting opens/Save As fail rather than racing temporary/backup files.
Select clients round-robin. Intermediate pending versions are replaceable because
the last completed checkpoint remains durable and the live document remains owned
by its session; acceptance is never reported as completion.

Ctrl+S/Save As, discard, suspend and close fence the client, discard its pending
snapshot, and join its active transaction. Save stays synchronous. Suspend/close
flush the latest dirty document before releasing ownership. Only completion of
the current document version means checkpoint-current; it never clears the
document's unsaved dirty flag. Errors preserve the previous valid record, drop
pending work, retain editing, and retry the latest version after backoff.

Use priority 1, no core affinity, one initial 6144-byte internal stack; measure its
high-water mark and memory delta before acceptance. HID priority 4 and NimBLE have
precedence. Existing runtime internal minimum is about 44 KiB. Do not use a PSRAM
stack for filesystem writes: this build lacks PSRAM XIP, and flash operations can
disable the external-memory cache. Local SDK cache/flash and FatFs sources were
checked; see also [ESP-IDF external RAM restrictions](https://docs.espressif.com/projects/esp-idf/en/v5.3/esp32s3/api-guides/external-ram.html)
and [flash concurrency](https://docs.espressif.com/projects/esp-idf/en/v5.2.8/esp32s3/api-reference/peripherals/spi_flash/spi_flash_concurrency.html).
Moving I/O off main does not prove that cache-off intervals disappear: measure
actual BLE reception-to-dispatch latency during writes.

Allocate snapshots on demand in PSRAM; free replaced pending before allocating its
replacement to avoid a third full-document copy. Per client at most two documents
(active plus pending), approximately 512 KiB at the document limit, with no fixed
maximum-size allocation for small documents. The shared worker adds just one
internal stack. Worker creation failure must be visible; normal ticks must never
silently fall back to synchronous filesystem I/O.

## Implementation and validation

### Implemented contract

`solar_os_writer_checkpoint.c` owns a single internal-stack task and at most 16
clients. This is a bounded Writer-private service, not a filesystem API. Each
client owns copied document/recovery paths and reserves both until close. The
worker selects ready clients round-robin. Each client has at most one pending
record, with one globally active record: N clients need at most N+1 snapshots,
subject to allocation success. No live Writer/document pointer crosses the task
boundary. Client/control APIs are called only by the application task. USB shell
reboot is marshalled through the existing session operation queue, with the caller
blocked and retaining its request/context until the application task reboots.
This prevents concurrent client close/free or new submissions during shutdown.

The new `.recovery` record has a 200-byte explicitly little-endian header: magic
and format, document length, cursor, scroll anchor, zoom, 64-bit document version,
saved/discarded flag, checksum, and full document path. Raw UTF-8 follows. Checksum
calculation and file verification run on the worker; main only allocates and copies
the gap buffer plus metadata. Legacy text `.recovery` and `.meta` remain readable;
new metadata is authoritative. A zero-body saved record is a durable tombstone,
not an empty recovered document. New records require the new reader on downgrade.
Saved Markdown document format is unchanged.

Mutation increments the session's 64-bit document version. The worker client also
has a monotonically assigned runtime generation. requested includes attempted
snapshots; submitted counts accepted snapshots; inflight/pending identify owned
snapshots; completed changes only after safe-replace succeeds. A checkpoint is
current only when completed equals the live version. Document dirty means
"not explicitly saved" and is never cleared by background completion.

Before replacing a pending snapshot its memory is freed, so allocation failure
cannot transiently require a third full copy. Failure drops any pending work,
retains the previous durable file and the live buffer, and schedules a latest-state
retry after the existing two-second idle/backoff rule. Unchanged error messages
do not force another render. Worker startup failure rejects opening Writer with
an explicit error; ordinary ticks never fall back to filesystem writes.

Ctrl+S fences this client, cancels pending, joins inflight, synchronously saves the
document with the original helper, then commits recovery tombstones. Save As checks
both destination claims first, invalidates old and new recovery, and rebinds only
after success. If document save succeeds but recovery cleanup fails, the UI says
so, keeps dirty state, and permits retry. No late worker can resurrect recovery.
Session suspend/close fence and commit the latest document with its metadata.
An unanswered recovery dialog is preserved through suspend/close. Close frees
the client only after I/O returns; closing the final client joins the worker.

System KEY suspend is a different path from session suspend. The added device
boundary drains accepted snapshots before soft suspend or explicit sleep; reboot
and deep sleep additionally join the shared task. These hooks do not change KEY
mapping/debounce, BLE behavior, display baseline or refresh policy. They preserve
completed checkpoints; they do not promise to recover edits not yet snapshotted
at a device restart. A boundary I/O failure logs an error and retains the last
valid checkpoint. The existing void session-stop callback cannot keep a closing
session open on disk failure; Ctrl+S failure does keep editing active.

The safe-replace helper's new optional observer only adds timing boundaries. Its
backup/rollback/fsync/verification sequence is unchanged. A short-write fault was
added for testing. As before, sudden power loss between FAT renames, and failure
of the filesystem itself during rollback, are outside the atomicity guarantee.
Recovery reads do not adopt temp or backup files.

### Host validation

- Python suite: 546 tests, four expected skips, passed.
- Writer checkpoint concurrency test runs production worker code on pthread
  mutexes/notifications and actual files, with rename rejecting existing targets
  to match FatFs. It covers 1,000 requests (999 pending replacements), multiple
  clients sharing one task, path conflict, independent buffer lifetime, worker
  creation failure, snapshot allocation failure, open/write/short-write/flush/
  backup-rename/final-rename/verification failure, corrupted metadata rejection,
  legacy read, save/suspend/close while busy, shutdown drain/join, maximum-size
  256-KiB round-trip, and tracked snapshot/client/task/semaphore cleanup.
- Writer buffer/files, document layout, display async, SSD1677, BLE HID, input,
  PaperMono power, FT6336 and application-exit tests passed.
- Analyzer counts only actual BLE reception inside measured worker windows as
  overlapping input, keeps generation/version pairs separate, and deduplicates
  replayed logs. Ordinary typing alone is not evidence for busy latency.

### Builds and first hardware measurements

| Profile | Static RAM | Flash | firmware.bin SHA256 |
|---|---:|---:|---|
| Normal | 52,464 B | 3,314,475 B | `82fd53fe45ebf10c9d689d3a50420cadf4c41262af84cb75035012dd6a179ae0` |
| Bringup | 52,336 B | 3,310,251 B | `af964fa630d4c7cf79cea62890eb7a3567ad15635f49132d95c0b74cf6b4e5b6` |

Normal was flashed to COM39 with hash verification. Bringup was built and saved,
not flashed. The original `/async-test.md` was read and preserved before flashing;
tests use `/checkpoint-test.md`. Existing intermittent BLE reconnect delay
occurred once; restarting ZMK restored it without bond changes or BLE code edits.

First client allocation: internal 81399 -> 74799 B (6600 B), PSRAM 7473952 ->
7473540 B (412 B). Internal stack is 6144 B; observed unused stack minimum after
12 writes is 3044 B. Snapshots allocate length+201 B, at most 262345 B each; two
maximum records use 524690 B PSRAM. Snapshot bytes return to zero after completion.
Post-cycle internal free 77.5 KiB, historical minimum 41.7 KiB; PSRAM free/min
7.1 MiB. Startup comparison before the new firmware was 84.0 KiB internal free,
44.2 KiB minimum; dynamic BLE allocations mean the exact creation delta above is
more useful than subtracting unrelated runtime totals.

12 automated edit/idle cycles completed versions 2 through 13 with zero errors,
zero pending replacements (writes finished before the next edit), and zero retained
snapshot bytes. UTF-8 lengths 1..12 B: actual copy 30..40 us, median 31 us. Worker
duration 213.167..402.996 ms, median 350.898 ms. The previous 739.524 ms synchronous
stall also performed a separate metadata replacement; the new transaction includes
metadata, so these durations are not a same-format filesystem speed comparison.
Current recovery is `/.writer/200caabe.recovery`, 212 B. No matching `.writer.tmp`
or `.writer.bak` was left. Older `.meta.tmp` / `.recovery.tmp` from preceding port
work remain untouched and must not be misreported as leaks from this phase.

### Additional device measurements (2026-10-07)

Small-document `checkpoint-timed.log`: six correct `aabcde` trials; three actually
overlapped the worker. Six overlapping key reports dispatched in 1.112..21.316 ms
(median 13.510 ms); the other 30 keys had median 5.984 ms, maximum 9.715 ms.
The other three trials typed before the idle trigger and are not busy samples.
An earlier 31-key ordinary-input run had median 4.278 ms, maximum 9.796 ms.
Protocol mistakes and reported typos in `checkpoint-bc.log` are retained in raw
logs and are not counted as exact-sequence successes.

A separate `/checkpoint-large.md` fixture contains 261992 spaces followed by
`\nCP\n`, then test edits. Its first 261997-byte snapshot copied in 11.989 ms;
the worker completed in 2.393 s. Allocation was 262198 B and returned to zero.
This is a near-limit device measurement; the exact 262144-byte case is host-tested.
The blank prefix is synthetic and is not representative prose.

Two subsequent exact `aabcde` trials both overlapped writes, yielding five exact
overlapping trials in total when combined with the three small-document trials.
There was no reported display corruption or missing/reordered text. However,
the large-document run **does not meet ordinary-input latency**: the seven busy
keys had median 547.029 ms, maximum 1307.881 ms. Document update (`doc_us`) took
947..1549 ms during these writes, versus about 255..273 ms without a write;
snapshot copy remained about 12 ms. This establishes a remaining document
parse/layout stall amplified during flash activity, not a wait in the checkpoint
submission API. It does not isolate the contributions of CPU/cache contention.
Do not claim the large-document responsiveness problem is fixed. Parser/layout
incrementality and flash/cache contention measurement are the next candidates;
dirty rectangles cannot address this measured stall.

Host validation rerun after reboot routing: Python 546 tests, four skips, passed
under WSL; checkpoint concurrency/files and application-exit tests passed. An
initial Windows-native Python run failed due to missing `make` and cp932 decoding;
the Linux suite is the supported validation used here.

All three near-limit `a` + 20-digit trials were received in exact order. They had
7, 5 and 5 key reports inside active writes. The first run's maximum busy delay
was 1662.078 ms; the two repeats reached 998.470 ms. The same large-document update
limitation applies. PC cue audibility interrupted the first attempt after trial
one; those isolated `a` edits are not counted as 20-character trials.

Real BLE Ctrl+S arrived at 998395965 us during version 81's write. The fence ran
998431218..998888518 us (457.300 ms) and joined the worker before document save.
The tombstone boundary completed successfully at 1001984438 us. Explicit save
remains synchronous and can block main; this is intentional durability behavior,
not a background-checkpoint latency sample. Saving/rebinding resets client version
counters (current 82, requested/completed zero); the document is clean, with a
durable tombstone rather than recoverable text.

KEY1 suspend encountered active=1, pending=0 at 1028207946 us. Version 83 completed
at 1028254163 us; drain returned at 1028254520 us (46.574 ms), then entered suspend.
On resume, `RESUME` and a real Ctrl+S were received; the save boundary succeeded.
KEY2 was initially omitted by the user; a separate KEY2/newline + `k`, four-corner
touch and Ctrl+S check then passed by user observation and saved text readback.
The final file length was 262085 B. Its test suffix (before the original `\nCP\n`)
matches two `aabcde` and three 20-digit sequences, isolated retry `a` edits,
`RESUME\nk`. CTRL+END had placed the test insertion before the original `CP`
line; the user could not see CP during typing. Existing EOF/caret behavior was
recorded and left unchanged. Recovery after save was a 200-byte SOLCP record with
saved flag 1. After closing Writer: internal free 83.9 KiB, historical low 42.0;
PSRAM free 7.3 MiB, low 5.5 (includes the subsequent Python readback allocation).
An optional Python full-prefix comparison exceeded that VM's heap; this is not
a checkpoint allocation failure. File length/suffix and tombstone readback passed.

Latest rebuilt profiles (reboot marshalling; application binary only):

| Profile | Static RAM | Flash | SHA256 |
|---|---:|---:|---|
| Normal | 52,464 B | 3,315,155 B | `b216847e4646634412cf3d6c1d2406e1f0bb5d145a2b406edab1096b0f70e8fd` |
| Bringup | 52,336 B | 3,310,803 B | `ce7b0cb73f9ac914a884f681876e326689f3343a77f406c84424ae94d2333476` |

Normal was flashed to COM39 with esptool hash verification. Bringup is built and
saved, never flashed to the acceptance device.

Controlled recovery passed on the final normal firmware. USB-injected `RECOVER`
completed version 8 at 47366457 us; an additional `X` was queued and allowed to
dispatch before a software reboot, before the next idle checkpoint. No power was
cut. After restart, the verified destination was 207 B, body exactly `RECOVER`,
length/cursor 7, anchor 0, zoom 1, version 8, saved flag 0, correct document path.
The user selected `r` in Recovery found, confirmed `RECOVER` with no `X`, and saved
through ZMK Ctrl+S. The save/tombstone boundary completed successfully at 129512402
us in the new boot. Reading the saved file returned exactly `RECOVER`; its recovery
was then a 200-B saved tombstone. Session close followed by reopening the saved file
succeeded. No new-test temp or backup files remained. Final BLE was connected to
the remembered keyboard; log level info and CDC sink off, with COM39 released.

The minimum contract is met: ordinary tick checkpoint filesystem operations run
on the worker, owned snapshots survive live edits/close safely, and currentness is
version-checked. Small-document overlapping input approaches ordinary latency.
The near-limit synthetic document **does not meet the practical latency target**;
its parse/layout update still blocks dispatch, especially during flash writes.
This is an explicit unresolved performance result, not an all-document success.

### Requested completion checklist

| # | Topic | Result / location in this report |
|---|---|---|
| 1 | Task validity | Pre-implementation review: proceed; idle-delay-only changes cannot remove blocking. |
| 2 | Previous architecture | Main tick, two-second idle, gap-buffer flatten, recovery plus separate metadata replacements. |
| 3 | Async architecture | One shared Writer-specific worker, reserved paths, bounded per-client pending. |
| 4 | Alternatives | Deep parsed-document copies, shared mutable state, per-Writer tasks and generic I/O service considered above. |
| 5 | Snapshot | Owned immutable UTF-8 plus 200-byte metadata header; checksum/verification on worker. |
| 6 | Versions | Runtime generation and 64-bit document version; old completion cannot mark newer edits current. |
| 7 | Inflight/pending | One globally active, one latest pending per client; replaced memory freed. |
| 8 | Save | Synchronous document replacement and durable tombstone after fence/join. |
| 9 | Suspend | Drain accepted snapshots for system suspend; session suspend commits latest dirty state. |
| 10 | Shutdown/close | Application-task boundaries; join worker before freeing client/last service or rebooting. |
| 11 | Error | Previous valid record retained; editing continues; visible deduplicated error. |
| 12 | Retry | Existing two-second idle/backoff; retry latest state, no synchronous tick fallback. |
| 13 | Task | Priority 1, no affinity, one 6144-B internal stack; observed minimum unused 3012 B. |
| 14 | Memory | Exact creation delta and runtime/near-limit figures above; 262345 B maximum snapshot. |
| 15 | Files | Listed below; prior port changes retained. |
| 16 | Host | Production worker pthread/fault/concurrency tests, ASAN/UBSAN/leak checks, relevant suites passed. |
| 17 | Builds | Normal and bringup passed; final hashes above; only normal flashed. |
| 18 | Checkpoint time | Small median 350.898 ms; near-limit first 2392.951 ms. |
| 19 | Busy BLE latency | Small median 13.510 ms / max 21.316 ms; near-limit limitation explicitly reported above. |
| 20 | Five-character input | Five exact overlapping trials: three small, two near-limit; no missing/reordered characters. |
| 21 | Twenty-character input | Three exact overlapping trials, all 60 digits received in order. |
| 22 | Loss/order | Actual key timestamps plus saved-file suffix and user observations; mistakes excluded. |
| 23 | Replacement count | Physical workload zero; host 999 replacements for 1000 pending requests. |
| 24 | Completed/current | Small cycles 13/13; large pre-save 92/92; no retained snapshot bytes. Save resets checkpoint counters. |
| 25 | Ctrl+S busy | Real BLE key, 457.300-ms fence, document/tombstone success and readback. |
| 26 | Suspend busy | Active write drained in 46.574 ms; resume, retained text and additional input passed. |
| 27 | Recovery | Controlled restart restored completed v8 `RECOVER`, excluded uncheckpointed `X`; real BLE recovery/save/readback and close/reopen passed. |
| 28 | Temp/backup | No matching new-test leftovers after completed small/large transactions; pre-existing artifacts retained. |
| 29 | 1000 requests/leaks | Passed; tracked snapshots, clients, semaphores and task resources return to zero on last close. |
| 30 | Regressions | Async display/BLE input, KEY1/KEY2, touch, USB shell, save tested; relevant host suites passed. |
| 31 | Unresolved | Near-limit document update/cache contention, prior EOF/caret and intermittent BLE reconnect; FAT power-cut atomicity not claimed. |
| 32 | Next work | Profile/incrementalize document parse/layout and quantify flash/cache stalls before dirty rectangles. |

Files changed specifically for this phase: `src/apps/solar_os_writer_checkpoint.[ch]`,
`solar_os_writer.c`, `solar_os_writer_files.[ch]`, `src/main.c`, `src/solar_os.c`,
`src/services/solar_os_sessions.[ch]`, `packages/solar_os_packages.toml`,
`tests/host/writer_checkpoint_test.c`, `writer_files_test.c`,
`app_exit_result_test.c`, `tests/host/Makefile`, checkpoint host stubs,
`scripts/analyze_writer_checkpoint.py`, `tests/test_writer_checkpoint_analysis.py`,
and this report. Existing display/input/board changes in the working tree belong
to earlier phases and are not part of this optimization.
