/* Laboratory candidates, compiled only with SOLAR_OS_SSD1677_WAVEFORM_TRIAL.
 * Derived from the M5GFX baseline in epd_ssd1677_reference.inc (same license).
 * SSD1677 Rev 1.0 sections 6.6/6.7 specify the encoding, not suitability for
 * this panel. DC balance and lifetime are UNVERIFIED. Never select at boot.
 * Ten waveform frames and all FR/voltage bytes stay identical to baseline;
 * this does NOT prove equal BUSY time, equal electric dose, or panel safety.
 */
static const uint8_t ssd1677_trial_dose_lut[110] = {
    0,0,0,0,0,0,0,0,0,0,
    0x6a,0xa0,0,0,0,0,0,0,0,0,
    0x95,0x50,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    /* Changed pixels: reverse pulse 2->1; destination pulse 8->9.
     * Redistribute inside group 0; keep each group's frame count unchanged. */
    1,3,2,2,0, 1,1,0,0,0,
    0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,0,
    0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,0,
    0x8f,0x8f,0x8f,0x8f,0x8f,
    0x17,0x46,0xa8,0x32,0x30,
};

static const uint8_t ssd1677_trial_hold_lut[110] = {
    /* Retained black: VSL for 2 frames, VSH1 for 2, VSS for 6.
     * Equal frame counts do not establish DC balance (VCOM/amplitudes differ). */
    0x90,0,0,0,0,0,0,0,0,0,
    0x6a,0xa0,0,0,0,0,0,0,0,0,
    0x95,0x50,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    2,2,2,2,0, 1,1,0,0,0,
    0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,0,
    0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,0,
    0x8f,0x8f,0x8f,0x8f,0x8f,
    0x17,0x46,0xa8,0x32,0x30,
};
