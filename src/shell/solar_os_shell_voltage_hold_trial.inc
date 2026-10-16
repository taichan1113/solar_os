/* Bounded voltage-register A/B using the established fixed-black image.
 * USB serial only. Each condition performs one cold + six warm activations. */
#include "epd_ssd1677.h"
#include <limits.h>
#include <stdlib.h>

static void fixed_black_trial_white(u8g2_t *g)
{
    u8g2_SetMaxClipWindow(g);
    u8g2_ClearBuffer(g);
    u8g2_SetDrawColor(g, 1);
    u8g2_DrawBox(g, 0, 0, 800, 480);
}

static void fixed_black_trial_image(u8g2_t *g)
{
    fixed_black_trial_white(g);
    u8g2_SetDrawColor(g, 0);
    u8g2_SetFont(g, u8g2_font_solar_os_default_r_20_tf);
    u8g2_SetFontMode(g, 1);
    u8g2_SetFontPosBaseline(g);
    u8g2_DrawStr(g, 40, 86, "FIXED BLACK 0123456789");
    u8g2_DrawStr(g, 40, 118, "The quick brown fox");
    u8g2_DrawBox(g, 40, 150, 48, 48);
    u8g2_DrawHLine(g, 40, 222, 280);
    u8g2_DrawStr(g, 430, 118, "RIGHT:");
    u8g2_DrawStr(g, 430, 150, "a");
}

static unsigned fixed_black_trial_changed(const uint8_t *a, const uint8_t *b, size_t n)
{
    unsigned changes = 0;
    for (size_t i = 0; i < n; ++i)
        changes += (unsigned)__builtin_popcount((unsigned)(a[i] ^ b[i]));
    return changes;
}

/* U8g2 keeps native 800x480 bytes even when the logical view uses R2.
 * Under R2, logical left x=0..399 resides in native columns 400..799. */
static uint32_t fixed_black_trial_left_hash(const u8g2_t *g, const uint8_t *frame)
{
    uint32_t hash = UINT32_C(2166136261);
    const size_t first_column = g->cb == U8G2_R2 ? 400 : 0;
    for (size_t tile_y = 0; tile_y < 60; ++tile_y)
        for (size_t x = first_column; x < first_column + 400; ++x)
            hash = (hash ^ frame[tile_y * 800 + x]) * UINT32_C(16777619);
    return hash;
}

/* A serial gate leaves the image untouched for T0/T1 photographs. */
static bool fixed_black_trial_gate(solar_os_shell_io_t *io, const char *stage)
{
    solar_os_shell_io_printf(io, "VH gate=%s g=next q=abort\n", stage);
    solar_os_shell_io_flush(io);
    for (;;) {
        uint8_t key = 0;
        size_t n = 0;
        const esp_err_t err = solar_os_port_read(&io->port, &key, 1, 0, &n);
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) return false;
        if (n && key == 'g') return true;
        if (n && (key == 'q' || key == 27 || key == 3)) return false;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static esp_err_t fixed_black_trial_present(solar_os_shell_io_t *io,
                                           solar_os_display_target_t *target,
                                           epd_ssd1677_t *d,
                                           const char *phase,
                                           unsigned expected_changes,
                                           bool fast, bool force_identical)
{
    const unsigned changed = fixed_black_trial_changed(d->shadow, d->buffer,
                                                         d->buffer_size);
    if (expected_changes != UINT_MAX && changed != expected_changes)
        return ESP_ERR_INVALID_STATE;
    const uint32_t before_0x20 = d->trial_fast_0x20_count;
    const uint32_t before_0x32 = d->trial_fast_0x32_count;
    const uint32_t before_0x03 = d->trial_fast_0x03_count;
    const uint32_t before_0x04 = d->trial_fast_0x04_count;
    const uint32_t before_0x2c = d->trial_fast_0x2c_count;
    const bool expected_hold = d->voltage_hold_trial && d->voltage_hold_ready &&
                               d->reference_awake && d->controller_ready;
    d->trial_force_identical_fastest = force_identical;
    uint64_t id = 0;
    esp_err_t err = solar_os_display_submit(target->u8g2,
                                            SOLAR_OS_DISPLAY_PRESENT_GRAPHICS, &id);
    if (err == ESP_OK) err = solar_os_display_wait(target->u8g2, 70000);
    d->trial_force_identical_fastest = false;
    solar_os_display_submission_status_t status = {0};
    if (err == ESP_OK) err = solar_os_display_submission_status(target->u8g2, &status);
    if (err == ESP_OK && (status.error != ESP_OK || status.completed_id != id ||
                          status.busy || status.pending || d->faulted ||
                          !d->shadow_valid || !d->papermono_baseline_valid ||
                          memcmp(d->shadow, d->buffer, d->buffer_size)))
        err = ESP_ERR_INVALID_STATE;
    const unsigned issued = d->trial_fast_0x20_count - before_0x20;
    const unsigned lut_writes = d->trial_fast_0x32_count - before_0x32;
    const unsigned gate_writes = d->trial_fast_0x03_count - before_0x03;
    const unsigned source_writes = d->trial_fast_0x04_count - before_0x04;
    const unsigned vcom_writes = d->trial_fast_0x2c_count - before_0x2c;
    if (err == ESP_OK && fast && (issued != 1 || d->timing.activations != 1 ||
                                  d->actual_spi_hz != 20000000 || lut_writes != 1 ||
                                  gate_writes != (expected_hold ? 0U : 1U) ||
                                  source_writes != gate_writes || vcom_writes != gate_writes))
        err = ESP_ERR_INVALID_STATE;
    if (err == ESP_OK && !fast && (issued || lut_writes || gate_writes ||
                                    source_writes || vcom_writes)) err = ESP_ERR_INVALID_STATE;
    const epd_ssd1677_timing_t *t = &d->timing;
    solar_os_shell_io_printf(io,
        "VH phase=%s err=%d changed=%u spi_hz=%d cmd03=%u cmd04=%u cmd2c=%u "
        "cmd32=%u cmd20=%u total_fast_0x20=%lu activations=%u total_us=%lld busy_us=%lld spi_us=%lld\n",
        phase, err, changed, d->actual_spi_hz,
        gate_writes, source_writes, vcom_writes, lut_writes, issued,
        (unsigned long)d->trial_fast_0x20_count, t->activations,
        (long long)(t->end_us - t->start_us),
        (long long)(t->ready_us[0] - t->activate_us[0]),
        (long long)(t->spi_us[0] + t->spi_us[1] + t->spi_us[2] + t->spi_us[3]));
    return err;
}

static void display_cmd_voltage_hold_trial(solar_os_shell_io_t *io,
                                           int argc, char **argv)
{
    static const char owner[] = "shell:voltage-hold";
    if (argc != 5 || strcmp(argv[2], "display0") ||
        (strcmp(argv[4], "A") && strcmp(argv[4], "B")) ||
        solar_os_shell_io_kind(io) != SOLAR_OS_SHELL_IO_KIND_PORT) {
        solar_os_shell_io_writeln(io,
            "usage (USB serial): display test display0 voltage-hold A|B");
        return;
    }
    const char condition = argv[4][0];
    solar_os_display_target_t target;
    if (!solar_os_display_find_target("display0", &target) || !target.ready ||
        !target.u8g2 || strcmp(target.driver, "ssd1677")) {
        solar_os_shell_io_writeln(io, "VH rejected: ready SSD1677 display0 required");
        return;
    }
    epd_ssd1677_t *d = (epd_ssd1677_t *)((uint8_t *)target.u8g2 -
                                          offsetof(epd_ssd1677_t, u8g2));
    if (!d->papermono_otp || !d->fastest_tiles || !d->band_buffer ||
        d->fastest_spi_hz != 20000000 ||
        d->waveform_trial != EPD_SSD1677_WAVEFORM_BASELINE ||
        (target.u8g2->cb != U8G2_R0 && target.u8g2->cb != U8G2_R2) ||
        u8g2_GetDisplayWidth(target.u8g2) != 800 ||
        u8g2_GetDisplayHeight(target.u8g2) != 480 || target.black_is_one ||
        d->buffer_size != 48000 || d->buffer != u8g2_GetBufferPtr(target.u8g2) ||
        !d->shadow) {
        solar_os_shell_io_writeln(io,
            "VH rejected: PaperMono 800x480/20MHz/tile/baseline required");
        return;
    }
    char busy_owner[SOLAR_OS_DISPLAY_TARGET_OWNER_MAX];
    esp_err_t err = solar_os_display_claim(target.name, owner,
                                            busy_owner, sizeof(busy_owner));
    if (err != ESP_OK) {
        solar_os_shell_io_printf(io, "VH claim err=%d owner=%s\n", err, busy_owner);
        return;
    }
    const epd_ssd1677_refresh_mode_t previous_refresh = d->refresh_mode;
    const bool previous_timing = d->timing_enabled;
    const bool previous_voltage_hold = d->voltage_hold_trial;
    const int previous_configured_spi_hz = d->configured_spi_hz;
    bool configured = false, healthy = false;
    unsigned completed = 0;
    unsigned buffer_interference = 0;
    uint8_t *initial_frame = NULL;
    err = solar_os_display_wait(target.u8g2, 70000);
    if (err != ESP_OK || d->faulted) goto done;
    healthy = true;
    configured = true;
    /* Keep the OTP setup/cleanup at 20 MHz as well as the fastest steps. */
    d->configured_spi_hz = 20000000;
    err = solar_os_display_set_controller_mode(target.name, "refresh=fastest");
    if (err == ESP_OK) err = solar_os_display_set_controller_mode(target.name, "timing=on");
    if (err == ESP_OK) err = solar_os_display_set_controller_mode(target.name,
                                        condition == 'B' ? "voltage-hold=on" : "voltage-hold=off");
    if (err != ESP_OK) goto done;
    solar_os_shell_io_printf(io,
        "VH begin condition=%c steps=6 spacing_ms=2000 spi_mhz=20 "
        "path=tile waveform=baseline image=common\n", condition);
    fixed_black_trial_white(target.u8g2);
    err = solar_os_display_set_controller_mode(target.name, "refresh=cleanup");
    if (err == ESP_OK) err = fixed_black_trial_present(io, &target, d, "white-otp",
                                                        UINT_MAX, false, false);
    if (err != ESP_OK) { healthy = false; goto done; }
    if (!fixed_black_trial_gate(io, "white")) goto done;
    fixed_black_trial_image(target.u8g2);
    err = solar_os_display_set_controller_mode(target.name, "refresh=cleanup");
    if (err == ESP_OK) err = fixed_black_trial_present(io, &target, d, "initial-otp",
                                                        UINT_MAX, false, false);
    if (err != ESP_OK) { healthy = false; goto done; }
    d->reference_awake = false; /* OTP full slept the controller; first fast is cold. */
    const uint32_t fixed_hash = fixed_black_trial_left_hash(target.u8g2, d->buffer);
    initial_frame = malloc(d->buffer_size);
    if (!initial_frame) { err = ESP_ERR_NO_MEM; goto done; }
    memcpy(initial_frame, d->buffer, d->buffer_size);
    solar_os_shell_io_printf(io, "VH fixed_left_hash=%08lx rotation=%s\n",
                             (unsigned long)fixed_hash,
                             target.u8g2->cb == U8G2_R2 ? "R2" : "R0");
    err = solar_os_display_set_controller_mode(target.name, "refresh=fastest");
    if (err != ESP_OK) goto done;
    /* Both conditions perform the same first custom update, before T0. */
    err = fixed_black_trial_present(io, &target, d, "initial-fast",
                                    0, true, true);
    if (err != ESP_OK) { healthy = false; goto done; }
    if (!fixed_black_trial_gate(io, "T0-photograph")) goto done;
    const int64_t start = esp_timer_get_time();
    for (unsigned step = 1; step <= 6; ++step) {
        const int64_t due = start + (int64_t)step * 2000000;
        while (esp_timer_get_time() < due) vTaskDelay(pdMS_TO_TICKS(5));
        const int64_t actual = esp_timer_get_time();
        /* A foreground app may edit the shared U8g2 buffer while our claim
         * blocks physical presents. Restore the exact T0 frame each time. */
        if (memcmp(d->buffer, initial_frame, d->buffer_size)) ++buffer_interference;
        memcpy(d->buffer, initial_frame, d->buffer_size);
        const uint32_t current_hash = fixed_black_trial_left_hash(target.u8g2, d->buffer);
        if (current_hash != fixed_hash) {
            solar_os_shell_io_printf(io,
                "VH invalid-left step=%u expected=%08lx actual=%08lx\n",
                step, (unsigned long)fixed_hash, (unsigned long)current_hash);
            err = ESP_ERR_INVALID_STATE;
            goto done;
        }
        err = fixed_black_trial_present(io, &target, d, "fast-step", 0, true, true);
        if (err != ESP_OK) { healthy = false; goto done; }
        completed++;
        solar_os_shell_io_printf(io,
            "VH step=%u condition=%c elapsed_ms=%lld scheduled_ms=%u "
            "total_fast_0x20=%lu\n", step, condition,
            (long long)((actual - start) / 1000), step * 2000,
            (unsigned long)d->trial_fast_0x20_count);
    }
    solar_os_shell_io_printf(io, "VH T1 condition=%c completed=%u (photograph now)\n",
                             condition, completed);
    (void)fixed_black_trial_gate(io, "T1-photograph");
done:
    d->trial_force_identical_fastest = false;
    free(initial_frame);
    if (configured) {
        if (healthy) {
            esp_err_t cleanup = solar_os_display_set_controller_mode(target.name,
                                                                      "refresh=cleanup");
            if (cleanup == ESP_OK)
                cleanup = fixed_black_trial_present(io, &target, d, "exit-otp",
                                                    UINT_MAX, false, false);
            if (cleanup != ESP_OK) {
                healthy = false;
                if (err == ESP_OK) err = cleanup;
            }
        }
        const char *refresh[] = {"refresh=auto", "refresh=partial", "refresh=full",
                                 "refresh=fastest", "refresh=typing",
#if SOLAR_OS_SSD1677_HYBRID_TRIAL
                                 "refresh=hybrid",
#endif
        };
        (void)solar_os_display_set_controller_mode(target.name, refresh[previous_refresh]);
        (void)solar_os_display_set_controller_mode(target.name,
                 previous_timing ? "timing=on" : "timing=off");
        (void)solar_os_display_set_controller_mode(target.name,
                 previous_voltage_hold ? "voltage-hold=on" : "voltage-hold=off");
        d->configured_spi_hz = previous_configured_spi_hz;
    }
    (void)solar_os_display_release(target.name, owner);
    if (configured && healthy && solar_os_display_target_is_board_primary(target.name)) {
        (void)solar_os_display_set_controller_mode(target.name, "refresh=cleanup");
        solar_os_sessions_mark_foreground_dirty();
    }
    solar_os_shell_io_printf(io,
        "VH end condition=%c err=%d completed=%u healthy=%u buffer_interference=%u total_fast_0x20=%lu\n",
        condition, err, completed, healthy, buffer_interference,
        (unsigned long)d->trial_fast_0x20_count);
}
