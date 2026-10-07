# PaperMono text contrast experiment

## Baseline and diagnosis

Baseline commit: `2935fd94` (terminal, Writer, BLE and platform services).
Experiment branch: `experiment/papermono-text-contrast`.

The user reports progressive lightening during typing. The configured clock is
**40 MHz SPI**, not 40 Hz panel refresh. SPI controls image/command transfer;
the LUT determines the electrical pixel drive. Earlier 10/20/40 MHz transport
tests found no missing pixels or transfer corruption, and 20 MHz did not improve
suspend grain. This does not prove that clock has no optical effect, but waveform
and accumulated differential updates are a stronger initial hypothesis.

Current fastest uses a short differential LUT: unchanged black/white select
no-drive groups. Optical fading is not observable from the digital shadow, which
can remain correct while the panel's contrast changes. A source framebuffer or
shadow comparison alone cannot validate the user's observation.

Manufacturer references:

- [M5Stack PaperMono notes](https://docs.m5stack.com/en/core/PaperMono): periodic full refresh after partial refresh, custom waveform balance.
- [Good Display update modes](https://www.good-display.com/news/134.html): speed versus repeated fast-update ghosting.
- [Official M5GFX SSD1677 implementation](https://github.com/m5stack/M5GFX/blob/c5a3fefad0b38a52cc750e2c4761e339a69a7208/src/lgfx/v1/panel/Panel_SSD1677.inl): `lut_fast_SSD1677` and `_refresh_mode2_absolute`.

## Trial implementation

`refresh=balanced` uses the **unmodified 48-frame M5GFX absolute fast LUT**.
Every changed image re-drives both black and white endpoints over the whole panel,
including letters already on the screen. It may trade more latency and visible
flicker for steadier contrast. It skips identical frames; it does not add cursor
refreshes, rewrite Writer scheduling or assume a measured optical improvement.

Both full RAM banks receive the **inverted current snapshot**: group0 selects
white, group3 black. Reusing fastest's current/old pair would incorrectly select
gray groups for transitions. Scalar and optimized tile conversion agree. The
same 40 MHz transfer and immutable latest-frame executor remain in use.

The LUT, including its voltage bytes, matches the official M5GFX source and the
user's installed reference Writer byte-for-byte. No trial voltage or timing
constant was invented. This is a different waveform profile; reducing SPI alone
does not lengthen its pixel-driving phases.

Official source file SHA256:
`0bfca3f6438a17447a30b88771a20828dfb96546197aedccffb6daaacd52ac98`.

Installed Writer `Panel_SSD1677.cpp` SHA256:
`c87a4c6a95a24d50d4d2750fe99215c655d673b040337523e3ace13fba093f58`.

Copied LUT (110 bytes) SHA256:
`034524ed38cfce3f511a5af1128add03830142ae4f1dafe7719639e428b97394`.

Switching among fastest/balanced/OTP first establishes an OTP full baseline.
Explicit cleanup remains effective even on an identical image. Suspend uses the
existing analog-off/BUSY/deep-sleep sequence; resume establishes a full baseline.
Shadow promotion occurs only after successful writes and BUSY completion; an
I/O fault invalidates history and latches failure without automatic retries.

## Controls

```text
display mode display0 refresh=balanced
display mode display0 refresh=fastest
display mode display0 refresh=cleanup
display mode display0 refresh=partial
```

The first two select the trial and the previous fast mode. `cleanup` queues the
existing full cleanup on the next present; an idle Writer may need a cursor move
to submit a frame. `partial` selects the existing, slower OTP fast waveform.
SPI comparison remains available with `spi-mhz=20` / `spi-mhz=40`.

These are runtime settings. The saved startup script retains `refresh=fastest`;
a reboot returns to it. Boot persistence should be changed only after choosing
the preferred optical result. No documents, zoom4, JP mapping, touch/RTC/LED
policy or BLE code are changed by this experiment.

## Verification

- SSD1677 host tests pass existing fastest and new balanced paths: endpoint
  encoding, two identical current planes, unchanged old black marks, scalar/tile,
  immutable snapshot under concurrent render mutation, mode crossing, cleanup,
  suspend/full recovery, failed SPI, BUSY timeout and unchanged completed shadow
  on failure.
- Existing async display executor regression passes.
- SSD1677 tests also pass AddressSanitizer and UBSan.
- All 110 LUT bytes independently compared with official and installed sources.
- Normal firmware build and app-only COM39 flash pass. Startup fastest, JP/font,
  touch OFF, LED OFF, RTC and remembered BLE are retained. User optical testing
  is in progress; improvement is not yet claimed.

Changed production files: `src/drivers/epd_ssd1677.h`, `epd_ssd1677.c`,
`epd_ssd1677_reference.inc`. Tests: `tests/host/epd_ssd1677_test.c`. Documentation:
this report and `doc/manual/commands.md`. BLE/platform/Writer implementation
remains the baseline commit.

## Device measurements and artifact

The same new Writer document was driven through generic `input emit` at an
interval long enough for the panel to finish. These are **driver durations,
not BLE end-to-end latency measurements**.

| Mode | Valid warm records | Driver median | BUSY median | SPI |
| --- | ---: | ---: | ---: | ---: |
| fastest | 12 | 113.949 ms | 51.683 ms | 40 MHz |
| balanced | 8 | 315.058 ms | 249.787 ms | 40 MHz |

Twelve characters were submitted per mode. On the first balanced input, mode
crossing required a 4,371.228 ms OTP full baseline and then a 733.287 ms cold
host-LUT update. Inputs during those operations were coalesced into the latest
frame, giving eight warm records rather than twelve. Only complete successful
warm records enter the medians; the changeover delay is excluded and disclosed
here. Native logs and USB echo can interleave, so incomplete records would also
be excluded rather than given invented durations. The measurement script's
first attempt queried the generic log ring, which does not contain these native
ESP_LOGI records; the successful run parses actually received USB timing records.
The first save injection used an unsupported chord spelling; use `CTRL+S` for
generic injection, or the physical keyboard Ctrl+S. Manual confirmation must
include saving, not assume an invalid injection saved the document.

Firmware: `build/papermono-text-contrast/firmware.bin` (3,340,400 bytes).

SHA256: `d0012cdad78791036f1e467a9551e78c10529ad4e4f9d04c3c0ea720c842d249`.

ELF SHA256: `ff82f9b782b2fe78bd7d54f03979a6b907553fc634ea43434301a37f56780a1e`.

Used app flash 3,334,615 B (**+332 B** over platform baseline); reported static
RAM 52,904 B (**unchanged**). App-only write at `0x10000` was hash verified.
No NVS/FAT erase or bond change. Automatic typing uses only the new
`/contrast-trial.md`; existing documents are not edited.

Evidence: `.research/contrast/build.log`, `flash.log`, `trial.log`, `measure.log`,
`results.json`. Timing is OFF and CDC log sink OFF after the comparison. Writer
remains in balanced for the user's typing test; COM39 is released.
