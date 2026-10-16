/* Real Driver/SPI plus the established retain/swap Controller RAM models. */
#define SSD1677_DIRTY_TEST_MAIN dirty_existing_main
#include "epd_ssd1677_dirty_hold_test.c"

static epd_ssd1677_t prepared(bool r2)
{
    epd_ssd1677_t d = make_display();
    d.papermono_otp = true; d.fastest_tiles = true; d.band_buffer = band;
    d.fastest_spi_hz = 20000000; d.configured_spi_hz = 20000000;
    d.dirty_hold_trial = d.dirty_hold_full_width = d.dirty_hold_dual_sync = true;
    d.u8g2.cb = r2 ? U8G2_R2 : U8G2_R0;
    frame[0] = 0x7f; frame[900] = 0x55; frame[47999] = 0x81;
    assert(epd_ssd1677_set_controller_mode(&d, "refresh=fastest") == ESP_OK);
    assert(ssd1677_refresh(&d) == ESP_OK); /* OTP baseline */
    assert(epd_ssd1677_black_hold_arm(&d, false) == ESP_OK);
    command_count = 0;
    assert(epd_ssd1677_present_snapshot(&d, frame, sizeof(frame)) == ESP_OK);
    assert(d.reference_awake && d.dirty_hold_ram_synced);
    expect_data(0x22, (uint8_t[]){0xcc}, 1);
    assert(d.timing.activations == 1);
    return d;
}

static void test_identical_ab(bool r2, bool swap)
{
    black_hold_candidate_spent = false; /* simulate a fresh boot in this Host case */
    epd_ssd1677_t d = prepared(r2);
    controller_ram_t *m = malloc(sizeof(*m)); assert(m);
    controller_seed(m, &d); m->swap_after_activation = swap;
    command_count = 0;
    assert(epd_ssd1677_present_snapshot(&d, frame, sizeof(frame)) == ESP_OK && !command_count);
    assert(epd_ssd1677_black_hold_arm(&d, false) == ESP_OK);
    assert(epd_ssd1677_present_snapshot(&d, frame, sizeof(frame)) == ESP_OK);
    assert(controller_apply(m, &d).activations == 1);
    expect_data(0x22, (uint8_t[]){0x0c}, 1);
    const size_t count_a = command_count;
    captured_command_t *a = malloc(sizeof(commands)); assert(a);
    memcpy(a, commands, sizeof(commands));
    command_count = 0;
    assert(epd_ssd1677_black_hold_arm(&d, true) == ESP_OK);
    assert(epd_ssd1677_present_snapshot(&d, frame, sizeof(frame)) == ESP_OK);
    assert(command_count == count_a);
    assert(controller_apply(m, &d).activations == 1);
    unsigned luts = 0, activations = 0;
    for (size_t i = 0; i < command_count; ++i) {
        assert(a[i].command == commands[i].command && a[i].length == commands[i].length);
        if (commands[i].command == 0x32 && luts++ == 0) {
            assert(commands[i].length == 105 && commands[i].data[0] == 0x8f && commands[i].data[1] == 0xf0);
            assert(!memcmp(a[i].data+2, commands[i].data+2, 103));
        } else assert(!memcmp(a[i].data, commands[i].data, a[i].length));
        if (commands[i].command == 0x20) ++activations;
    }
    assert(luts == 2 && activations == 1); /* second LUT restores baseline without activation */
    assert(!memcmp(find_command(0x32, 1)->data, ssd1677_reference_lut, 105));
    assert(m->classes[0] > 0 && m->classes[1] == 0 && m->classes[2] == 0 && m->classes[3] > 0);
    assert(!memcmp(m->bw, m->red, sizeof(m->bw)) && !memcmp(d.shadow, frame, sizeof(frame)));
    assert(!d.black_hold_active && !d.black_hold_pending && epd_ssd1677_black_hold_spent());
    assert(epd_ssd1677_black_hold_arm(&d, true) == ESP_ERR_INVALID_STATE);
    command_count = 0;
    assert(epd_ssd1677_present_snapshot(&d, frame, sizeof(frame)) == ESP_OK && !command_count);
    frame[15] ^= 1;
    assert(epd_ssd1677_present_snapshot(&d, frame, sizeof(frame)) == ESP_OK);
    expect_data(0x32, ssd1677_reference_lut, 105);
    free(a); free(m);
}

static void test_failure(unsigned fault)
{
    black_hold_candidate_spent = false;
    epd_ssd1677_t d = prepared(false);
    d.reference_awake = false;
    assert(epd_ssd1677_black_hold_arm(&d, true) == ESP_ERR_INVALID_STATE);
    assert(!epd_ssd1677_black_hold_spent());
    d.reference_awake = true; d.powered = false;
    assert(epd_ssd1677_black_hold_arm(&d, true) == ESP_ERR_INVALID_STATE);
    d.powered = true; d.shadow_valid = false;
    assert(epd_ssd1677_black_hold_arm(&d, true) == ESP_ERR_INVALID_STATE);
    d.shadow_valid = true;
    assert(epd_ssd1677_black_hold_arm(&d, true) == ESP_OK);
    command_count = 0;
    if (fault == 0) fail_command = 0x32;
    if (fault == 1) busy_on_activation = true;
    if (fault == 2) frame[100] ^= 1; /* stale/interfering snapshot rejected before SPI */
    assert(epd_ssd1677_present_snapshot(&d, frame, sizeof(frame)) != ESP_OK);
    if (fault == 2) assert(command_count == 0);
    assert(!d.black_hold_active && !d.black_hold_pending && epd_ssd1677_black_hold_spent());
    assert(epd_ssd1677_black_hold_arm(&d, true) == ESP_ERR_INVALID_STATE);
    stuck_busy = false; busy_on_activation = false; fail_command = 0;
    d = prepared(false);
    assert(epd_ssd1677_black_hold_arm(&d, true) == ESP_ERR_INVALID_STATE); /* display reset doesn't refund */
}

int main(void)
{
    unsigned decoded[4];
    waveform_frames(ssd1677_trial_black_hold_single_lut, 0, decoded);
    assert(decoded[0] == 2 && decoded[1] == 0 && decoded[2] == 2 && decoded[3] == 6);
    assert(-15 * (int)decoded[2] + 5 * (int)decoded[3] == 0);
    for (unsigned row = 1; row < 5; ++row) {
        unsigned original[4];
        waveform_frames(ssd1677_trial_black_hold_single_lut, row, decoded);
        waveform_frames(ssd1677_reference_lut, row, original);
        assert(!memcmp(decoded, original, sizeof(decoded)));
    }
    epd_ssd1677_baseline_main(); /* unchanged normal Driver regression, with flag enabled */
    for (unsigned r = 0; r < 2; ++r) for (unsigned s = 0; s < 2; ++s) test_identical_ab(r, s);
    for (unsigned f = 0; f < 3; ++f) test_failure(f);
    black_hold_candidate_spent = false;
    epd_ssd1677_t d = prepared(false);
    assert(epd_ssd1677_black_hold_arm(&d, true) == ESP_OK);
    epd_ssd1677_black_hold_cancel(&d);
    assert(!d.black_hold_pending && epd_ssd1677_black_hold_spent());
    assert(epd_ssd1677_set_controller_mode(&d, "fastest-waveform=black-hold-single") == ESP_ERR_INVALID_ARG);
    assert(epd_ssd1677_set_controller_mode(&d, "fastest-waveform=hold") == ESP_ERR_INVALID_ARG);
    puts("black-single: R0/R2 retain/swap, RAM/Shadow/classes, command-only LUT diff, one-shot/restore/fault gates PASS");
    return 0;
}
