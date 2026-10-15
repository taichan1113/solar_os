# PaperMono fastest fixed-black trial

This is a trial-only serial command. It leaves the normal Writer update path unchanged.
Build for `m5paper_mono` / `writerdeck` with `SOLAR_OS_SSD1677_FIXED_BLACK_TRIAL=1`,
`SOLAR_OS_SSD1677_SPI20_TRIAL=1`, `SOLAR_OS_SSD1677_HYBRID_TRIAL=1`, and
`SOLAR_OS_SSD1677_WAVEFORM_TRIAL=0`. Flash only the application partition after
checking the live chip, partition table, active OTA slot, current app backup, and
NVS/FAT/otadata/PHY protection. Do not use `firmware.factory.bin`.

## Operator sequence

Use the USB serial Shell, one condition at a time:

```
display test display0 fixed-black A
display test display0 fixed-black B
display test display0 fixed-black C
```

The command rejects an on-device Shell, non-PaperMono display, non-baseline LUT,
non-tile path, or a clock other than 20 MHz. It claims `display0` and first shows
white with a normal OTP full update. `FB gate=white` waits for `g`. The same
fixed-left/variable-right image is then shown with an OTP full update.
`FB gate=T0-photograph` waits while the initial image is photographed. Shoot at
roughly the same delay and with fixed camera position, lighting, exposure,
focus, and white balance in all three conditions. Send `g` to begin the timed
segment. Do not touch the device during the 12-second segment.

At 2-second offsets, the command runs six steps:

| Condition | Each step | Expected fastest `0x20` count |
| --- | --- | ---: |
| A | Wait, no frame submission | 0 |
| B | Resend identical old/new RAM through the existing fastest LUT | 6 |
| C | Append one right-side character, keeping the left half unchanged | 6 |

`FB gate=T1-photograph` holds the completed frame. Photograph it at the same
post-update delay used for T0, then send `g` or `q` to leave the command. A
normal OTP cleanup restores the Shell image. Keep the original files; image
contrast is an optical observation, not a claim that Host RAM state proves it.

For B, the trial-only one-shot flag bypasses both identical-frame skips. It
still uses the normal fastest tile transport, original M5GFX-derived LUT,
0x24/0x26 full-frame writes and 0x20 activation. The log checks one actual
successful 0x20 command per step. The flag is cleared after each submission
and on every exit. It is never set by Writer.

## Logs and stop conditions

Capture USB output beginning before the first command. Each completed update
reports changed pixels, actual SPI clock, 0x20 delta and cumulative count,
total time, BUSY time, and SPI time. The left-half framebuffer hash is checked
after every step. Stop on any inversion, strong ghosting, visible worsening,
BUSY/SPI error, unexpected update count, or mismatched image; do not retry or
run additional cycles. On a hardware error the command does not trigger a
recovery refresh. Record panel temperature if available and keep the time
between conditions as similar as practical.

The firmware and serial log cannot measure black density; record T0/T1 images
and a short visual assessment for each condition. After all three, restore the
backed-up application-only firmware and verify the Shell plus protected data.
