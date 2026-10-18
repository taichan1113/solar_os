#if SOLAR_OS_SSD1677_BLACK_HOLD_PRIME_AA_TRIAL
#if SOLAR_OS_SSD1677_BLACK_HOLD_DAD_TRIAL
#define BLACK_HOLD_FIXED_COMMAND "black-hold-dad"
#define BLACK_HOLD_FIXED_CONDITION "DAD"
#else
#define BLACK_HOLD_FIXED_COMMAND "black-hold-d-prime-aa"
#define BLACK_HOLD_FIXED_CONDITION "DAA"
#endif
#endif

/* Bounded operator-gated diagnostic, reusing the fixed-black image and
 * existing display claim / immutable executor submission / OTP cleanup. */
#include "epd_ssd1677.h"
#include "esp_timer.h"
#include <stdlib.h>

static void black_single_white(u8g2_t *g)
{
    u8g2_SetMaxClipWindow(g);
    u8g2_ClearBuffer(g);
    u8g2_SetDrawColor(g, 1);
    u8g2_DrawBox(g, 0, 0, 800, 480);
}

static void black_single_image(u8g2_t *g)
{
    black_single_white(g);
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

static bool black_single_gate(solar_os_shell_io_t *io, const char *stage)
{
    solar_os_shell_io_printf(io, "BH gate=%s g=next q=stop\n", stage);
    solar_os_shell_io_flush(io);
    for (;;) {
        uint8_t key = 0;
        size_t n = 0;
        esp_err_t err = solar_os_port_read(&io->port, &key, 1, 0, &n);
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) return false;
        if (n && key == 'g') return true;
        if (n && (key == 'q' || key == 27 || key == 3)) return false;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* Video markers only: no display request. Anchor both conditions to completed
 * Driver timestamps rather than operator response or accumulated log delays. */
static bool black_single_wait_until(solar_os_shell_io_t *io, const char *stage,
                                    int64_t anchor_us, uint32_t delay_ms)
{
    const int64_t target_us = anchor_us + (int64_t)delay_ms * 1000;
    while (esp_timer_get_time() < target_us) {
        uint8_t key = 0;
        size_t n = 0;
        esp_err_t err = solar_os_port_read(&io->port, &key, 1, 0, &n);
        if ((err != ESP_OK && err != ESP_ERR_TIMEOUT) ||
            (n && (key == 'q' || key == 27 || key == 3))) {
            solar_os_shell_io_printf(io, "BH aborted wait=%s\n", stage);
            return false;
        }
        vTaskDelay(1);
    }
    solar_os_shell_io_printf(io,
        "BH mark=%s anchor_us=%lld target_us=%lld actual_us=%lld\n", stage,
        (long long)anchor_us, (long long)target_us, (long long)esp_timer_get_time());
    solar_os_shell_io_flush(io);
    return true;
}

#if SOLAR_OS_SSD1677_BLACK_HOLD_REPEAT3_TRIAL
static unsigned black_single_attempts(epd_ssd1677_black_hold_t candidate)
{
#if SOLAR_OS_SSD1677_BLACK_HOLD_PRIME_AA_TRIAL
    (void)candidate;
    return epd_ssd1677_black_hold_attempts(EPD_SSD1677_BLACK_HOLD_A) +
           epd_ssd1677_black_hold_attempts(EPD_SSD1677_BLACK_HOLD_D);
#else
    return epd_ssd1677_black_hold_attempts(candidate);
#endif
}
#endif

static esp_err_t black_single_present(solar_os_shell_io_t *io,
    solar_os_display_target_t *target, epd_ssd1677_t *d, const char *phase,
    const uint8_t *image, bool forced, epd_ssd1677_black_hold_t candidate, bool prearmed)
{
    uint32_t before[5];
    memcpy(before, d->black_hold_commands, sizeof(before));
    /* Buffer may have been edited while the display claim blocked a present.
     * Every submit restores the canonical image. Driver also rejects a forced
     * snapshot differing from completed shadow, before issuing any command. */
    if (image) memcpy(d->buffer, image, d->buffer_size);
    esp_err_t err = prearmed ?
        (forced && d->black_hold_pending == (uint8_t)candidate + 1U ? ESP_OK : ESP_ERR_INVALID_STATE) :
        forced ? epd_ssd1677_black_hold_arm(d, candidate) : ESP_OK;
    uint64_t id = 0;
    if (err == ESP_OK)
        err = solar_os_display_submit(target->u8g2, SOLAR_OS_DISPLAY_PRESENT_GRAPHICS, &id);
    if (err == ESP_OK) err = solar_os_display_wait(target->u8g2, 70000);
    epd_ssd1677_black_hold_cancel(d); /* submit/wait failure cannot leave an arm */
    solar_os_display_submission_status_t status = {0};
    if (err == ESP_OK) err = solar_os_display_submission_status(target->u8g2, &status);
    if (err == ESP_OK && (status.error != ESP_OK || status.completed_id != id ||
        status.busy || status.pending || d->faulted || !d->shadow_valid ||
        !d->papermono_baseline_valid || (image && memcmp(d->shadow, image, d->buffer_size))))
        err = ESP_ERR_INVALID_STATE;
    unsigned delta[5];
    for (unsigned i = 0; i < 5; ++i) delta[i] = d->black_hold_commands[i] - before[i];
    if (err == ESP_OK && forced && (delta[0] != 1 || delta[1] != 1 || delta[2] != 1 ||
        delta[3] != 2 || delta[4] != 1 || d->timing.activations != 1 ||
        d->actual_spi_hz != 20000000 || !d->reference_awake)) err = ESP_ERR_INVALID_STATE;
    const epd_ssd1677_timing_t *t = &d->timing;
#if SOLAR_OS_SSD1677_BLACK_HOLD_PRIME_AA_TRIAL
    /* Separate bounded records: the old preparation line exceeded 256 bytes. */
    solar_os_shell_io_printf(io,
        "BH phase=%s err=%d candidate=%u spi_hz=%d cmd03=%u cmd04=%u cmd2c=%u cmd32=%u cmd20=%u\n",
        phase, err, candidate, d->actual_spi_hz, delta[0], delta[1], delta[2], delta[3], delta[4]);
    solar_os_shell_io_printf(io,
        "BH timing phase=%s start_us=%lld end_us=%lld total_us=%lld busy_us=%lld spi_us=%lld ram_bytes=%u\n",
        phase, (long long)t->start_us, (long long)t->end_us, (long long)(t->end_us-t->start_us),
        (long long)(t->ready_us[0]-t->activate_us[0]),
        (long long)(t->spi_us[0]+t->spi_us[1]+t->spi_us[2]+t->spi_us[3]),
        (unsigned)(t->ram_data_bytes[0]+t->ram_data_bytes[1]+t->ram_data_bytes[2]+t->ram_data_bytes[3]));
    if (forced) solar_os_shell_io_printf(io,
        "BH lut phase=%s requested_lut0=%s lut_send_ok=%u baseline_restore_ok=%u physical_readback=0\n",
        phase, candidate == EPD_SSD1677_BLACK_HOLD_D ? "2FF0" : "0000",
        d->black_hold_lut_ready_us > 0, err == ESP_OK);
#else
    solar_os_shell_io_printf(io,
        "BH phase=%s err=%d candidate=%u spi_hz=%d cmd03=%u cmd04=%u cmd2c=%u "
        "cmd32=%u cmd20=%u start_us=%lld end_us=%lld total_us=%lld busy_us=%lld spi_us=%lld ram_bytes=%u\n",
        phase, err, candidate, d->actual_spi_hz, delta[0], delta[1], delta[2], delta[3], delta[4],
        (long long)t->start_us, (long long)t->end_us, (long long)(t->end_us-t->start_us),
        (long long)(t->ready_us[0]-t->activate_us[0]),
        (long long)(t->spi_us[0]+t->spi_us[1]+t->spi_us[2]+t->spi_us[3]),
        (unsigned)(t->ram_data_bytes[0]+t->ram_data_bytes[1]+t->ram_data_bytes[2]+t->ram_data_bytes[3]));
#endif
    if (forced) solar_os_shell_io_printf(io,
        "BH trace phase=%s lut_ready_us=%lld activate_call_us=%lld busy_wait_end_us=%lld "
        "restore_start_us=%lld restore_end_us=%lld busy_edge_measured=0\n", phase,
        (long long)d->black_hold_lut_ready_us, (long long)t->activate_us[0],
        (long long)t->ready_us[0], (long long)d->black_hold_restore_start_us,
        (long long)d->black_hold_restore_end_us);
    return err;
}

static void display_cmd_black_hold_single(solar_os_shell_io_t *io, int argc, char **argv)
{
    static bool started[2]; /* one diagnostic session per condition and boot */
    static bool failed; /* no next condition after a diagnostic failure */
    static const char owner[] = "shell:black-single";
#if SOLAR_OS_SSD1677_BLACK_HOLD_PRIME_AA_TRIAL
    const char *usage = "usage (USB serial): display test display0 " BLACK_HOLD_FIXED_COMMAND;
    if (argc != 4 || strcmp(argv[2], "display0") || strcmp(argv[3], BLACK_HOLD_FIXED_COMMAND) ||
        solar_os_shell_io_kind(io) != SOLAR_OS_SHELL_IO_KIND_PORT) {
        solar_os_shell_io_writeln(io, usage);
        return;
    }
    const unsigned index = 0;
    const epd_ssd1677_black_hold_t candidate = EPD_SSD1677_BLACK_HOLD_D;
    const char *condition = BLACK_HOLD_FIXED_CONDITION;
#else
#if SOLAR_OS_SSD1677_BLACK_HOLD_REPEAT3_TRIAL
    const char *first = "A";
    const char *usage = "usage (USB serial): display test display0 black-hold-repeat3 A|D";
#else
    const char *first = "B";
    const char *usage = "usage (USB serial): display test display0 black-hold-single B|D";
#endif
    if (argc != 5 || strcmp(argv[2], "display0") ||
        (strcmp(argv[4], first) && strcmp(argv[4], "D")) ||
        solar_os_shell_io_kind(io) != SOLAR_OS_SHELL_IO_KIND_PORT) {
        solar_os_shell_io_writeln(io, usage);
        return;
    }
    const unsigned index = argv[4][0] == 'D';
#if SOLAR_OS_SSD1677_BLACK_HOLD_REPEAT3_TRIAL
    const epd_ssd1677_black_hold_t candidate = index ? EPD_SSD1677_BLACK_HOLD_D : EPD_SSD1677_BLACK_HOLD_A;
#else
    const epd_ssd1677_black_hold_t candidate = index ? EPD_SSD1677_BLACK_HOLD_D : EPD_SSD1677_BLACK_HOLD_B;
#endif
    const char *condition = argv[4];
#endif
    if (started[index] || failed || epd_ssd1677_black_hold_blocked() || epd_ssd1677_black_hold_spent(candidate) ||
        !solar_os_sessions_foreground_is_shell()) {
        solar_os_shell_io_writeln(io, "BH rejected: single-use budget or foreground Shell required");
        return;
    }
    solar_os_display_target_t target;
    if (!solar_os_display_find_target("display0", &target) || !target.ready ||
        !target.u8g2 || strcmp(target.driver, "ssd1677")) return;
    epd_ssd1677_t *d = (epd_ssd1677_t *)((uint8_t *)target.u8g2 - offsetof(epd_ssd1677_t, u8g2));
    if (!d->papermono_otp || d->faulted || !d->fastest_tiles || !d->band_buffer ||
        d->fastest_spi_hz != 20000000 || d->waveform_trial != EPD_SSD1677_WAVEFORM_BASELINE ||
#if SOLAR_OS_SSD1677_BLACK_HOLD_REPEAT3_TRIAL
#if SOLAR_OS_SSD1677_DIRTY_HOLD_TRIAL
        !d->dirty_hold_trial || !d->dirty_hold_full_width || d->dirty_hold_full_height || !d->dirty_hold_dual_sync ||
#else
        true || /* Repeat3 requires the unchanged rows-dual RAM path. */
#endif
#endif
        (target.u8g2->cb != U8G2_R0 && target.u8g2->cb != U8G2_R2) ||
        u8g2_GetDisplayWidth(target.u8g2) != 800 || u8g2_GetDisplayHeight(target.u8g2) != 480 ||
        target.black_is_one || d->buffer_size != 48000 || !d->shadow ||
        d->buffer != u8g2_GetBufferPtr(target.u8g2)) {
        solar_os_shell_io_writeln(io, "BH rejected: healthy PaperMono 800x480/20MHz/tile/baseline required");
        return;
    }
    char busy[SOLAR_OS_DISPLAY_TARGET_OWNER_MAX];
    esp_err_t err = solar_os_display_claim(target.name, owner, busy, sizeof(busy));
    if (err != ESP_OK) {
        solar_os_shell_io_printf(io, "BH claim err=%d owner=%s\n", err, busy);
        return;
    }
    const epd_ssd1677_refresh_mode_t previous_refresh = d->refresh_mode;
    const bool previous_timing = d->timing_enabled;
    const int previous_clock = d->configured_spi_hz;
    bool healthy = false;
#if SOLAR_OS_SSD1677_BLACK_HOLD_REPEAT3_TRIAL
    unsigned completed = 0;
    uint32_t diagnostic_cmd20_start = d->black_hold_commands[4];
#endif
    uint8_t *image = NULL;
    err = solar_os_display_wait(target.u8g2, 70000);
    if (err != ESP_OK || d->faulted) goto done;
    image = malloc(d->buffer_size);
    if (!image) { err = ESP_ERR_NO_MEM; goto done; }
    started[index] = true;
    d->configured_spi_hz = 20000000;
    (void)solar_os_display_set_controller_mode(target.name, "timing=on");
    black_single_white(target.u8g2);
    memcpy(image, d->buffer, d->buffer_size);
    err = solar_os_display_set_controller_mode(target.name, "refresh=cleanup");
    if (err == ESP_OK) err = black_single_present(io, &target, d, "white-otp", image, false, EPD_SSD1677_BLACK_HOLD_BASELINE, false);
    if (err != ESP_OK) goto done;
    healthy = true;
    if (!black_single_gate(io, "white")) goto done;
    black_single_image(target.u8g2);
    memcpy(image, d->buffer, d->buffer_size);
    err = solar_os_display_set_controller_mode(target.name, "refresh=cleanup");
    if (err == ESP_OK) err = black_single_present(io, &target, d, "image-otp", image, false, EPD_SSD1677_BLACK_HOLD_BASELINE, false);
    if (err != ESP_OK) { healthy = false; goto done; }
    d->reference_awake = false; /* OTP full ends with sleep */
    err = solar_os_display_set_controller_mode(target.name, "refresh=fastest");
    if (err == ESP_OK) err = black_single_present(io, &target, d, "prepare-baseline", image, true, EPD_SSD1677_BLACK_HOLD_BASELINE, false);
    if (err != ESP_OK) { healthy = false; goto done; }
    const int64_t prepared_us = d->timing.end_us;
#if SOLAR_OS_SSD1677_BLACK_HOLD_REPEAT3_TRIAL
    if (!black_single_wait_until(io, "T0", prepared_us, 5000)) goto done;
    solar_os_shell_io_printf(io,
        "BH progress condition=%s completed=0 boot_attempts=%u measured_cmd20=0\n",
        condition, black_single_attempts(candidate));
#if SOLAR_OS_SSD1677_BLACK_HOLD_PRIME_AA_TRIAL
    int64_t previous_end_us = prepared_us;
#endif
    for (unsigned step = 1; step <= 3; ++step) {
        char gate[24], mark[16], phase[24];
        snprintf(gate, sizeof(gate), "T%u-next", step-1);
        if (!black_single_gate(io, gate)) goto done;
        const int64_t requested_us = esp_timer_get_time();
        /* Reserve before the cancellable wait. No register is written by arm;
         * cancelling now still consumes one boot-global attempt. */
#if SOLAR_OS_SSD1677_BLACK_HOLD_PRIME_AA_TRIAL
        const epd_ssd1677_black_hold_t step_candidate = (step == 1 || (SOLAR_OS_SSD1677_BLACK_HOLD_DAD_TRIAL && step == 3)) ? EPD_SSD1677_BLACK_HOLD_D : EPD_SSD1677_BLACK_HOLD_A;
#else
        const epd_ssd1677_black_hold_t step_candidate = candidate;
#endif
        err = epd_ssd1677_black_hold_arm(d, step_candidate);
        if (err != ESP_OK) { healthy = false; goto done; }
        solar_os_shell_io_printf(io,
            "BH request condition=%s step=%u requested_us=%lld boot_attempts=%u\n",
            condition, step, (long long)requested_us, black_single_attempts(candidate));
        snprintf(mark, sizeof(mark), "measure%u", step);
#if SOLAR_OS_SSD1677_BLACK_HOLD_PRIME_AA_TRIAL
        const int64_t goal_us = previous_end_us + 60000000;
        const int64_t approved_us = requested_us + 5000000;
        const int64_t planned_us = goal_us > approved_us ? goal_us : approved_us;
        solar_os_shell_io_printf(io,
            "BH schedule condition=" BLACK_HOLD_FIXED_CONDITION " step=%u previous_end_us=%lld goal_us=%lld requested_us=%lld planned_us=%lld\n",
            step, (long long)previous_end_us, (long long)goal_us, (long long)requested_us, (long long)planned_us);
        if (!black_single_wait_until(io, mark, planned_us, 0)) goto done;
#else
        if (!black_single_wait_until(io, mark, requested_us, 5000)) goto done;
#endif
        snprintf(phase, sizeof(phase), "%s-repeat-%u", condition, step);
        err = black_single_present(io, &target, d, phase, image, true, step_candidate, true);
        if (err != ESP_OK) { healthy = false; goto done; }
        completed = step;
#if SOLAR_OS_SSD1677_BLACK_HOLD_PRIME_AA_TRIAL
        solar_os_shell_io_printf(io,
            "BH interval condition=" BLACK_HOLD_FIXED_CONDITION " step=%u end_to_start_us=%lld lateness_us=%lld\n",
            step, (long long)(d->timing.start_us-previous_end_us), (long long)(d->timing.start_us-goal_us));
        previous_end_us = d->timing.end_us;
#endif
        snprintf(mark, sizeof(mark), "T%u", step);
        if (!black_single_wait_until(io, mark, d->timing.end_us, 2000)) goto done;
        solar_os_shell_io_printf(io,
            "BH progress condition=%s completed=%u boot_attempts=%u measured_cmd20=%u\n",
            condition, completed, black_single_attempts(candidate), completed);
    }
    (void)black_single_gate(io, "T3-cleanup");
#else
    /* White gate authorizes the whole timed single shot while video runs.
     * T0: +5s; measurement: +10s; T1: +2s from completed measurement. */
    if (!black_single_wait_until(io, "T0", prepared_us, 5000)) goto done;
    if (!black_single_wait_until(io, "measure", prepared_us, 10000)) goto done;
    err = black_single_present(io, &target, d, index ? "D-single" : "B-single", image, true, candidate, false);
    if (err != ESP_OK) { healthy = false; goto done; }
    if (!black_single_wait_until(io, "T1", d->timing.end_us, 2000)) goto done;
    (void)black_single_gate(io, "T1-photograph");
#endif
done:
    epd_ssd1677_black_hold_cancel(d);
    if (healthy && !d->faulted) {
        err = solar_os_display_set_controller_mode(target.name, "refresh=cleanup");
        if (err == ESP_OK) err = black_single_present(io, &target, d, "exit-otp", image, false, EPD_SSD1677_BLACK_HOLD_BASELINE, false);
    }
    if (err != ESP_OK || d->faulted) failed = true;
    const char *refresh[] = {"refresh=auto", "refresh=partial", "refresh=full",
                            "refresh=fastest", "refresh=typing",
#if SOLAR_OS_SSD1677_HYBRID_TRIAL
                            "refresh=hybrid",
#endif
    };
    (void)solar_os_display_set_controller_mode(target.name, refresh[previous_refresh]);
    (void)solar_os_display_set_controller_mode(target.name, previous_timing ? "timing=on" : "timing=off");
    d->configured_spi_hz = previous_clock;
    free(image);
    (void)solar_os_display_release(target.name, owner);
    if (healthy && err == ESP_OK && !d->faulted) solar_os_sessions_mark_foreground_dirty();
#if SOLAR_OS_SSD1677_BLACK_HOLD_REPEAT3_TRIAL
    solar_os_shell_io_printf(io,
        "BH end condition=%s err=%d healthy=%u completed=%u boot_attempts=%u diagnostic_cmd20=%u\n",
        condition, err, healthy, completed, black_single_attempts(candidate),
        (unsigned)(d->black_hold_commands[4] - diagnostic_cmd20_start));
#else
    solar_os_shell_io_printf(io, "BH end condition=%s err=%d healthy=%u candidate_spent=%u\n",
                            condition, err, healthy, epd_ssd1677_black_hold_spent(candidate));
#endif
}
