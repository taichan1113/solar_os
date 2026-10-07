# PaperMono fastest transport / conversion optimization

## Task validity review — before implementation (2026-10-07)

Proceed: measured warm driver245–253ms contains about61ms conversion,96ms polling
SPI and52ms BUSY. Another approximately33ms is completed-shadow pixel comparison.
The waveform is already short; optimizing software/transport is the highest-value
next step.96kB full new/old planes and the exact LUT/voltages/CC/0C activation,
baseline, latest-frame ownership and analog-off sleep sequence remain unchanged.

Current PaperMono bus is SPI2, GPIO15 SCLK/14 MOSI/16 CS/17 DC, mode0, DMA auto,
4096-byte maximum shared-bus transfer. SSD1677 uses100-byte internal DMA row
staging,960 polling row transfers per two-plane update; snapshots and shadow are
PSRAM. Reference installed M5GFX configures40MHz but that alone does not qualify
SolarOS40MHz. SPI configuration starts10MHz; actual divider frequency will be
queried using the installed ESP-IDF5.5.5 API. Its transaction override frequency
persists in the device timing configuration, so change it once at a boundary,
not on every row. OTP continues to use the original configured frequency.

Prototype in one firmware:10/20/40MHz runtime controls, scalar reference versus
optimized tile/band path, separate difference/convert/SPI/setup/BUSY/promotion
timings. Start with the scalar10MHz path, then equal optimized-path patterns at
all three clocks. Select a production clock only after visible correctness and
100+ repeated updates, prioritizing stability. Stop on a transport/BUSY failure.

Optimization candidate: transpose each8x8 U8g2 tile once using byte-safe bitwise
operations, instead of extracting the same8 columns for each of8 panel rows;
stage32 rows (3200B) per polling DMA transaction. No large lookup table, global
U8g2 format change, rectangular transfer or application device branch. Compare
vertical framebuffer bytes once rather than each pixel to find the same aligned
bounding rectangle. Each output will be checked bit-exact against scalar code.
Queued double-buffer DMA is deferred initially: the bus already supports DMA,
and larger polling transfers avoid ISR/queue/ownership complexity. Measure the
remaining conversion cost before deciding whether overlap would be worthwhile.

Wire floors for96kB are76.8/38.4/19.2ms at10/20/40MHz; current polling overhead
is additional. Combining fewer calls, tile conversion and byte differences can
plausibly save100ms or more without waveform changes. These are hypotheses, not
results. Stage allocation/failure/shadow advancement must preserve existing safety.

Writer already has Ctrl+= (also Ctrl+Shift+=) zoom-in and Ctrl+- zoom-out,
range0..4, initial1. Fonts12/14/16/18/20px, line heights14/16/18/20/22px;
zoom2 is the requested small increase. Metadata/recovery already stores zoom.
First test these existing BLE shortcuts. If accepted, use a generic compile-time
default with only PaperMono manifests selecting2; saved metadata/recovery wins.
Do not add a redundant shortcut or change the global default. A reset shortcut
is not currently present; repeated existing zoom-out/in reaches the desired level.

The user has a30-minute observation window starting16:45 JST. Prioritize hardware
frequency/quality, Writer latency/zoom and suspend checks; finish host coverage,
analysis and documentation autonomously afterward. The old saved documents are
preserved; hardware tests use a separate normal-sized test document.

## Final result — 2026-10-07

PaperMono's opt-in fastest backend now uses an empirically selected40MHz clock,
8x8 tile transposition, bytewise difference bounds and32-row DMA staging.
The matched warm pattern median fell from245.642ms to114.242ms. Real ZMK isolated
input completion fell from the previous281.958ms baseline to155.993ms (10inputs).
The user selected **zoom4 /20px**, so PaperMono's new-document default is4.
Existing document/recovery zoom overrides that default; other boards retain1.
Boot remains AUTO/OTP. No LUT, voltage, phase, current/old classification or
full-plane semantics changed.

The requested observation window started16:45:15 JST. The last user-dependent
check finished by17:14:19 JST, within30minutes. Subsequent measurements, file
verification, tests and final builds required no additional user interaction.

## SPI and conversion measurements

One candidate firmware, one unchanged waveform, with runtime scalar/tile and
10/20/40MHz controls. Identical scheduled patterns were rectangle/erase,
text/fine lines, corners, half-black/erase, black/white. White initialization
established an OTP baseline before fastest testing. Actual divider frequencies
were queried from the attached SPI device, not inferred from requested clocks.

Medians below are milliseconds; cold updates are excluded.

| Path / actual clock | Warm records | Difference | Conversion, both planes | SPI, both planes | BUSY | Command/setup | Promotion | Total |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Scalar /10MHz |9|33.585|61.303|96.127|51.358|1.756|0.980|245.642|
| Tile /10MHz |12|4.559|36.275|77.457|51.522|0.899|0.980|172.011|
| Tile /20MHz |10|4.499|36.233|39.041|51.360|0.828|0.994|133.205|
| Tile /40MHz |10|4.566|36.314|19.820|51.529|0.793|0.985|114.242|
| Tile /40MHz,100 repetitions |100|4.461|36.327|19.820|51.400|0.792|0.979|113.918|

Medians of separate phases need not sum to the median total. Command/setup is
the per-record residual after difference, conversion, SPI, BUSY and promotion;
it includes plane addressing and LUT/control commands. CPU scheduling/cache
interruptions are included in these wall-clock measurements. Ten patterns were
scheduled at each clock; the10MHz log also contains2background shell/status
refreshes around BLE reconnect, hence12records. Raw records remain available.

At40MHz, total falls53.5%, conversion40.8%, and SPI79.4% relative to scalar10MHz.
The wire floor is19.2ms for96kB; measured19.82ms is close to that floor. BUSY is
unchanged. The20→40MHz benefit is18.963ms, about14.2% of20MHz total.

The user observed all three pattern sets without corruption. At40MHz, another
100updates completed without SPI failure, timeout, shifted pixels, noise or
visible residual on the final white screen:110observed candidate updates.
The final normal binary subsequently logged279fastest activations during the
automatic Writer run, with0driver errors. These later updates were autonomous;
their optical quality was not separately observed by the user. The transport
algorithm, staging dimensions and waveform match the observed candidate.

### Architecture and invariants

The existing board bus is SPI2, GPIO15 SCLK/14 MOSI/16 CS/17 DC, mode0,
DMA auto, configured4096B maximum transfer. Snapshots and completed shadow are
PSRAM. Previously each plane used480100B polling transfers. Now each uses15
3200B polling DMA transfers from internal staging:30rather than960transactions
per activation. Polling DMA remains synchronous inside the existing display
worker; there is no new worker, queue, DMA lifetime or application lock.

For named buses, init resolves the actual bus host and checks
`spi_bus_get_max_transaction_len` before allocating3200B staging. An owned bus
requests a compatible limit. An unknown/insufficient limit or staging allocation
failure retains scalar100B transport. The active final hardware reports the tile
path, confirming the queried limit admits3200B. The actual numerical bus limit
was not printed;4096B is the board configuration, not a fabricated SDK readback.

The installed ESP-IDF5.5.5 transaction frequency override persists. The driver
changes it once at a boundary, queries `spi_device_get_actual_freq`, and reuses
the setting. OTP full/fast explicitly restores the original10MHz clock. Runtime
mode/frequency controls run through the existing serialized executor.

An8x8 tile is loaded bytewise into a64-bit value and transposed by three XOR/mask
swap stages. Byte loads handle nonaligned sources and do not depend on CPU byte
order. This replaces repeated per-pixel extraction for8panel rows. No64K lookup
table or U8g2 format/rotation change is introduced. Difference bounds compare
48000bytes once and use ctz/clz only for nonzero XOR bytes, instead of comparing
384000pixels. Bounds match the old aligned bounding rectangle.

Every activation still writes all48000B of0x24=new and all48000B of0x26=completed
old. The dirty rectangle is diagnostic only. The exact105B LUT plus gate/source/
VCOM tail,0x22=CC cold/0C warm and activation guard remain unchanged. The shadow
advances only after successful completion. SPI/BUSY failure invalidates the
baseline/shadow and latches a fault. Immutable snapshots remain the only worker
source; a mutable application buffer is never substituted during transfer.

Added memory:3200B internal DMA staging,16B timing fields, five32-bit clock fields,
one staging pointer/flag and structure padding. No additional48kB plane/snapshot
or persistent conversion table. Normal static RAM remains52464B. Flash usage is
3319183B versus3316683B before this phase (+2500B). Final active runtime reported
internal free74.3KiB, DMA free66.8KiB and PSRAM free7.1MiB. Display/checkpoint task
priorities and stacks are unchanged. Wall time is not a CPU utilization measurement.

## Writer timing and zoom

Completion means the driver's BUSY completion as associated with the immutable
generation, not an optical sensor or the time a human first perceives a glyph.
Explicit replacement chains are followed; cancelled/failed work is not silently
credited to an unrelated later completion.

| Case | Previous fastest baseline | This phase | Evidence / qualification |
|---|---:|---:|---|
| Isolated character |281.958ms|155.993ms median,123.590–184.238ms|10real ZMK inputs at zoom2; each completed before the next input. The existing4s analyzer grouping merged the last3; per-input generation completion proves all10isolated samples. |
| `abcde` tail |322.750ms|140.441ms median,132.842–170.943ms|5consecutive real ZMK `abcde` groups at zoom2. Each group completed before the next group's first key; requested8s idle was not followed. |
| `abcde` tail, final zoom4 |—|163.312ms median,132.937–184.821ms|5complete USB injection trials; this is not BLE latency. |
|20digits tail |390.302ms BLE|155.415ms median,154.074–166.432ms|3complete USB injection trials at zoom4. The user intentionally accepted10digits×3for the manual check; no new BLE20-digit comparison is claimed. |
| Sentence final `.` |—|159.455ms|USB at zoom4. The following Enter completed in41.606ms with the known trailing-empty-line behavior. Initial records rolled out of the256-entry ring; only the last character's directly matched completion is reported. |
|100digit wrap, final `0` |—|141.235ms|USB at zoom4; first records rolled out. Full source independently verified. |
| HOME/END/LEFT/RIGHT/Enter/Backspace |—|6inputs captured,4physical updates; last Backspace15.139ms|Last edit can have no pixel difference. Do not interpret it as a15ms physical panel activation. |
|35Enter scroll |—|Final Enter26.551ms;16physical updates in the retained tail|35newlines verified in recovery source. Tail can be nonphysical; no complete first-to-last timing is claimed. |

USB trials include shell scheduling/pacing and begin at WRITER_INPUT, while BLE
trials begin at KEY_REPORT. They are deliberately kept separate. The strict
final run's5-character and20-character records are complete with no malformed
events. Earlier autonomous diagnostic attempts had incomplete shell echo/prompt
boundaries; those raw logs are retained as `transport-auto-first/second.log` and
excluded from final latency claims. The final command producer waits for shell
completion and tolerates native IDF lines interrupting the echo/prompt. Long
sentence/wrap/scroll ring rollover is disclosed, rather than manufacturing lost
timestamps. The final recovery payload was independently read back and matched
all280expected bytes, including the sentence,100digits, cancelled Enter/Backspace,
35newlines and subsequent zoom-test text.

Existing zoom range is0..4. Body font sizes12/14/16/18/20px correspond to line
heights14/16/18/20/22px, affecting wrap, page height and cursor line dimensions.
Monospaced character widths are6/7/8/9/10px. On the800x480target, the existing
body viewport is788x458px: approximately112columns/28lines at zoom1 versus
78columns/20lines at zoom4 for plain monospaced content, before document spacing.
The caret remains one pixel wide with height returned by the layout run.
The heading/status font policy remains unchanged. Existing shortcuts are Ctrl+=
(also Ctrl+Shift+=) for increase, Ctrl+- for decrease. HID keyboard usage0x2E
handles the increase independently of Shift. The user confirmed it on a Japanese
physical keyboard whose printed symbols differ from the HID mapping. No new
shortcut or reset binding was added; Ctrl+0 was not previously a zoom reset.
For the shell/API, the existing generic `input emit CTRL_PLUS` / `CTRL_MINUS`
actions work after `session focus display0`; no dedicated Writer zoom command
was needed. The automatic tests used these existing names.

The first candidate used new-document zoom2, following the +1-step proposal.
The user found it small, tried the existing controls and selected **zoom4**.
Final PaperMono/bringup manifests select4 through the generic board default macro.
Other boards fall back to1. Saved metadata/recovery takes priority. Readback
confirmed the user's173B document, saved=true and zoom4; it was reopened after
final flashing and automatic tests. No global Writer hardware branch was added.

Short USB samples at zoom1/2/3/4 had tail completions152.038/151.961/152.850/163.312ms
(zoom4 is the5-trial median). Render medians were approximately1.805/1.642/1.601ms
and1.478–2.217ms across the initial zoom4 trials. Document content/length differs
slightly across these samples; they are a light smoke test, not a controlled font
benchmark. Larger documents reached about9–10ms render time. Full-plane transport
still writes96kB, so larger glyph bounds do not multiply SPI duration.

## Power, quality and regression limits

KEY2 newline followed by K, KEY1 suspend/resume, retained content, XYZ input,
four-corner touch and Ctrl+S were confirmed by the user. Touch had8pointer events
and a release before final reflash; counters reset on reboot. Final touch is ready.
BLE remembered1/1and reconnected automatically after final flashing. The physical
orientation and FT6336 mapping were not changed by this phase.

The required stop sequence remains0x22=03,0x20,BUSY,0x10=01, reset, rail-off.
Measured analog-off was160.391ms at40MHz and160.219ms at20MHz. Resume restored an
OTP full baseline at10MHz in4525.064ms, followed by a40MHz cold fastest update
in461.128ms (BUSY133.693ms), then normal warm updates. Drain/shadow/mode crossing
and fault semantics remain covered by the existing executor and driver tests.

**The user reported worse stopped-state grain than the previous firmware.**
The user considered it less relevant because no typing occurs while stopped;
20MHz looked about the same as40MHz, and resume full refresh restored the screen.
This is an unresolved optical regression, not a claim of grain elimination.
The comparison does not prove its cause. It did not reproduce as frequency-
dependent corruption in the observed patterns. Thus40MHz was selected for its
measured benefit, while fastest remains explicitly opt-in and AUTO/OTP remains
the boot policy. Existing lighter text, faint pattern residuals and temperature/
lifetime/DC-balance uncertainty are not newly qualified by this short test.

The pre-existing `sleep_cpu_configure` CPU-power-down error also appears under
the lowpower profile; it is present in the previous strategy logs. Successful
display rail-off must not be presented as proof of whole-device sleep current.
The known Writer trailing-empty-line Enter/Backspace cursor behavior remains.
Concurrent checkpoint flash I/O and synchronous explicit save can still affect
wall timings; transport optimization does not remove those costs.

## Verification and reproducibility

Passed host tests: SSD1677, display_async (including1000replacements), Writer
checkpoint concurrency/files, document layout, input/HID reports, PaperMono power
and FT6336. Document source/cursor hit-test round trips now run at every zoom0..4.
SSD1677 tests cover single-column256values, all65536byte-pair patterns, nonaligned
loads, random full planes, scalar/byte bounds, first/last pixels, white/black,
checkerboards, identical frames, exact current/old planes, failed SPI, BUSY timeout,
shadow nonadvancement, snapshot ownership, sleep/control ordering and40→10MHz OTP
restoration. The final SSD test also passes ASan/UBSan with leak detection.
Python board manifests31tests and the display/checkpoint analyzers5+1tests pass.
Both normal and bringup firmware builds pass; only normal was flashed.

Normal artifact: `build/papermono-transport-z4/firmware.bin`

SHA256: `9d3bc20b48fb681b4442574e78c0ea24c4aef972070cd3912c2d40f6ed8e0abc`

Bringup artifact: `build/papermono-transport-z4-bringup/firmware.bin`

SHA256: `c67e7a5318309f9c9652a2a94e4123ce7ad05be79ae989be98495fd60ccff04d`

The final normal app was written only at0x10000 on COM39 and esptool verified its
hash. Document/NVS partitions were preserved. Current `.pio` build artifacts are
bringup; use the saved NORMAL file above for a manual reflash. Final runtime is
Writer `/transport-test.md`, saved zoom4, fastest40MHz, connected BLE, timing off,
info log level and CDC sink off. COM39 is released. No commit was made.

Controls, valid only for PaperMono's fastest backend:

```text
display mode display0 refresh=fastest
display mode display0 spi-mhz=40
display mode display0 fastest-path=tile
display mode display0 timing=on
display mode display0 refresh=cleanup
display mode display0 refresh=auto
```

Clock accepts10/20/40MHz; tile/scalar is retained for controlled comparison.
Neither setting changes boot persistence. Cleanup forces a full baseline on the
next present, including an unchanged image. Frequency and path changes preserve
the optical baseline because the plane values and waveform are unchanged.

Evidence: `.research/hardware/transport-{scalar10,tile10,tile20,tile40,repeat40}.log`,
`transport-writer-{isolated,abcde,digits}.log`, `transport-regression.log`,
`transport-font-power20.log`, `transport-auto.log`, `transport-verify.log`,
`transport-driver-results.json`, `transport-writer-results.json`,
`transport-auto-results.json`. Measurement scripts and incomplete initial logs
remain in `.research`; verification retained the read-back binary payloads.

## Requested completion topics

| # | Topic | Result |
|---:|---|---|
|1|Task validity|Review above predates implementation; software/transport dominates BUSY.|
|2|SPI architecture|SPI2/mode0/DMA, internal staging, PSRAM snapshots; details above.|
|3|Actual clocks|Queried10/20/40MHz in the same firmware.|
|4|10MHz|Scalar245.642ms; tile172.011ms warm median.|
|5|20MHz|133.205ms, observed patterns normal.|
|6|40MHz|114.242ms, observed patterns normal.|
|7|Production clock|40MHz only for opt-in fastest.|
|8|Selection|110observed updates,0errors,18.963ms benefit over20MHz; stop grain similar at20/40.|
|9|SPI before/after|96.127→19.820ms for96kB.|
|10|Conversion architecture|Vertical U8g2 tile→MSB-first panel rows, current/old full planes.|
|11|Bottleneck|Repeated pixel extraction,384k comparisons,960small transactions.|
|12|Optimization|8x8 word transpose,48k-byte XOR bounds,32-row staging.|
|13|Conversion before/after|61.303→36.314ms.|
|14|Combined driver|245.642→114.242ms warm median.|
|15|BUSY|51.358→51.529ms; waveform unchanged.|
|16|Isolated Writer|10real BLE samples,281.958→155.993ms; zoom2 measurement.|
|17|abcde|5real groups140.441ms; final zoom4 USB5trials163.312ms.|
|18|20digits|Final zoom4 USB3trials155.415ms; manual10-digit substitution disclosed.|
|19|Sentence/wrap/edit/scroll|Source verified; direct tail measurements and ring limits above.|
|20|Repetition|110observed candidate updates;279final normal Writer activations.|
|21|Errors/corruption|0SPI/driver/BUSY errors; no corruption in observed patterns; optical limits explicit.|
|22|Quality|Typing normal; stopped grain worsened versus prior firmware, unresolved.|
|23|Suspend/resume|03/20/BUSY/10sequence,160ms off,4525ms full baseline, cold/warm verified.|
|24|Memory|3200B DMA stage plus small state; static RAM unchanged, flash+2500B.|
|25|CPU/tasks|Fewer polling calls; worker/task/ownership unchanged; wall time not CPU utilization.|
|26|Existing zoom|0..4fonts12..20px, metadata/recovery and layout retained.|
|27|Existing shortcuts|Ctrl+= /Ctrl+Shift+= increase,Ctrl+- decrease; Japanese keyboard confirmed.|
|28|Default zoom|PaperMono1→final4; initial2was too small; other boards1.|
|29|Added shortcuts|None.|
|30|Layout|20px body/22px line height; wrap/scroll/source coordinates tested at0..4.|
|31|User acceptance|User chosez4and saved it; readback confirmed4.|
|32|Regression|8related host executables, sanitizer,37Python tests,2firmware builds, hardware/file checks.|
|33|Unresolved|Stopped grain, existing trailing-empty-line caret behavior, PM CPU-power-down error, optical/long-term limits.|
|34|Next optimization|First reproduce grain with controlled prior/optimized images. For speed, benchmark32-bit/nibble tile conversion; queued double buffering can hide at most about20ms of current SPI and adds ownership complexity.|
