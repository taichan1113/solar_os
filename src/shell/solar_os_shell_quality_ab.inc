/* Bounded, operator-stepped comparison of existing waveforms. Trial builds only.
 * No storage writes, new waveform, automatic retries, or production pacing. */
#include "epd_ssd1677.h"

typedef struct { uint16_t x, y, w, h; } quality_ab_roi_t;

static quality_ab_roi_t quality_ab_marker(u8g2_t *g)
{
    return (quality_ab_roi_t){u8g2_GetDisplayWidth(g) / 2,
                             u8g2_GetDisplayHeight(g) / 3, 16, 16};
}

static void quality_ab_white(u8g2_t *g)
{
    u8g2_SetMaxClipWindow(g);
    u8g2_ClearBuffer(g);
    u8g2_SetDrawColor(g, 1);
    u8g2_DrawBox(g, 0, 0, u8g2_GetDisplayWidth(g), u8g2_GetDisplayHeight(g));
}

static void quality_ab_image(u8g2_t *g)
{
    const unsigned w = u8g2_GetDisplayWidth(g), h = u8g2_GetDisplayHeight(g);
    const quality_ab_roi_t marker = quality_ab_marker(g);
    quality_ab_white(g);
    u8g2_SetDrawColor(g, 0);
    u8g2_DrawBox(g, marker.x - 80, marker.y, 48, 48); /* near black */
    u8g2_DrawBox(g, 32, marker.y, 48, 48);            /* far black */
    u8g2_SetFont(g, u8g2_font_solar_os_default_r_20_tf);
    u8g2_SetFontMode(g, 1);
    u8g2_SetFontPosBaseline(g);
    u8g2_DrawStr(g, 32, 56, "FIXED BLACK 0123456789 AaBb");
    u8g2_DrawStr(g, 32, 88, "Only the small marker changes");
    u8g2_DrawHLine(g, 32, h / 2, w - 64);
    u8g2_DrawBox(g, 32, h / 2 + 24, w - 64, 3);
    u8g2_DrawFrame(g, 8, 8, w - 16, h - 16);
}

static void quality_ab_draw_marker(u8g2_t *g, unsigned ordinal)
{
    const quality_ab_roi_t r = quality_ab_marker(g);
    u8g2_SetDrawColor(g, ordinal % 2 ? 0 : 1);
    u8g2_DrawBox(g, r.x, r.y, r.w, r.h);
}

static unsigned quality_ab_changes(const uint8_t *old, const uint8_t *now, size_t n)
{
    unsigned changes = 0;
    for (size_t i = 0; i < n; ++i)
        changes += (unsigned)__builtin_popcount((unsigned)(old[i] ^ now[i]));
    return changes;
}

static uint32_t quality_ab_image_hash(const uint8_t *bytes, size_t n)
{
    uint32_t hash = UINT32_C(2166136261);
    for (size_t i = 0; i < n; ++i) hash = (hash ^ bytes[i]) * UINT32_C(16777619);
    return hash; /* diagnostic identity, not a cryptographic integrity proof */
}

/* Serial-only modal command: the foreground shell cannot redraw the same
 * framebuffer while this command owns it. Each 'g' authorizes ONE transition.
 * No progress counter or phase label is drawn on the panel. */
static bool quality_ab_gate(solar_os_shell_io_t *io, const char *stage)
{
    solar_os_shell_io_printf(io, "AB gate=%s g=next q=abort\n", stage);
    solar_os_shell_io_flush(io);
    for (;;) {
        uint8_t key = 0;
        size_t n = 0;
        const esp_err_t err = solar_os_port_read(&io->port, &key, 1, 0, &n);
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) return false;
        if (n && key == 'g') return true;
        if (n && (key == 'q' || key == 27 || key == 0x1d || key == 3)) return false;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static esp_err_t quality_ab_present(solar_os_shell_io_t *io,
                                   solar_os_display_target_t *target,
                                   epd_ssd1677_t *d, const char *phase,
                                   unsigned ordinal, bool fast)
{
    const unsigned changes = quality_ab_changes(d->shadow, d->buffer, d->buffer_size);
    const bool cold = !d->reference_awake;
    solar_os_shell_io_printf(io,
        "AB submit phase=%s ordinal=%u changed_pixels=%u cold=%u\n",
        phase, ordinal, changes, cold);
    uint64_t id = 0;
    esp_err_t err = solar_os_display_submit(target->u8g2,
                                           SOLAR_OS_DISPLAY_PRESENT_GRAPHICS, &id);
    if (err == ESP_OK) err = solar_os_display_wait(target->u8g2, 70000);
    solar_os_display_submission_status_t s = {0};
    if (err == ESP_OK) err = solar_os_display_submission_status(target->u8g2, &s);
    if (err == ESP_OK && (s.error != ESP_OK || s.completed_id != id || s.busy || s.pending))
        err = ESP_ERR_INVALID_STATE;
    if (err == ESP_OK && (!d->shadow_valid || d->faulted || d->snapshot != NULL ||
                         memcmp(d->shadow, d->buffer, d->buffer_size) != 0))
        err = ESP_ERR_INVALID_STATE;
    if (err == ESP_OK && fast && (!d->reference_awake || d->timing.writes != 2 ||
                                  d->timing.activations != 1 ||
                                  d->actual_spi_hz != 40000000))
        err = ESP_ERR_INVALID_STATE;
    const epd_ssd1677_timing_t *t = &d->timing;
    solar_os_shell_io_printf(io,
        "AB complete phase=%s ordinal=%u err=%d generation=%llu completed=%llu "
        "total_us=%lld busy_us=%lld convert_us=%lld spi_us=%lld\n",
        phase, ordinal, err, (unsigned long long)id, (unsigned long long)s.completed_id,
        (long long)(t->end_us - t->start_us),
        (long long)(t->ready_us[0] - t->activate_us[0]),
        (long long)(t->convert_us[0] + t->convert_us[1]),
        (long long)(t->spi_us[0] + t->spi_us[1]));
    return err;
}

static void display_cmd_quality_ab(solar_os_shell_io_t *io, int argc, char **argv)
{
    static const char owner[] = "shell:quality-ab";
    if (argc != 5 || (strcmp(argv[3], "ab-a") && strcmp(argv[3], "ab-b")) ||
        (strcmp(argv[4], "baseline") && strcmp(argv[4], "dose")) ||
        solar_os_shell_io_kind(io) != SOLAR_OS_SHELL_IO_KIND_PORT) {
        solar_os_shell_io_writeln(io,
            "usage (serial shell): display test display0 ab-a|ab-b baseline|dose");
        return;
    }
    solar_os_display_target_t target;
    if (!solar_os_display_find_target(argv[2], &target) || !target.ready ||
        !target.u8g2 || strcmp(target.driver, "ssd1677")) {
        solar_os_shell_io_writeln(io, "AB rejected: ready SSD1677 target required");
        return;
    }
    /* Board-primary targets use board ops instead of controller_context. The
     * native SSD1677 display owns its embedded U8g2 in both registration paths. */
    epd_ssd1677_t *d = (epd_ssd1677_t *)((uint8_t *)target.u8g2 - offsetof(epd_ssd1677_t, u8g2));
    /* Preserve the running SPI/tile configuration, rather than overriding it.
     * Only this known full-buffer mono geometry/polarity has been validated. */
    if (!d->papermono_otp || !d->fastest_tiles || d->fastest_spi_hz != 40000000 ||
        u8g2_GetDisplayWidth(target.u8g2) != 800 ||
        u8g2_GetDisplayHeight(target.u8g2) != 480 || target.black_is_one ||
        d->buffer_size != 48000 || d->buffer != u8g2_GetBufferPtr(target.u8g2) ||
        !d->shadow || !d->band_buffer) {
        solar_os_shell_io_writeln(io, "AB rejected: PaperMono 800x480/40MHz/tile required");
        return;
    }
    char busy_owner[SOLAR_OS_DISPLAY_TARGET_OWNER_MAX];
    esp_err_t err = solar_os_display_claim(target.name, owner, busy_owner, sizeof(busy_owner));
    if (err != ESP_OK) {
        solar_os_shell_io_printf(io, "AB claim error=%d owner=%s\n", err, busy_owner);
        return;
    }
    uint8_t *masks = solar_os_memory_alloc(d->buffer_size * 2,
                                          SOLAR_OS_MEMORY_EXTERNAL_REQUIRED, "quality-ab-masks");
    bool configured = false, healthy = false;
    const bool original_timing = d->timing_enabled;
    const epd_ssd1677_refresh_mode_t original_refresh = d->refresh_mode;
    const epd_ssd1677_waveform_trial_t original_waveform = d->waveform_trial;
    const bool retention = !strcmp(argv[3], "ab-b");
    unsigned fast_count = 0;
    if (!masks) { err = ESP_ERR_NO_MEM; goto done; }
    err = solar_os_display_wait(target.u8g2, 70000);
    if (err != ESP_OK || d->faulted) { healthy = false; goto done; }
    healthy = true;
    configured = true;
    err = solar_os_display_set_controller_mode(target.name, "refresh=fastest");
    if (err == ESP_OK) err = solar_os_display_set_controller_mode(target.name, "timing=on");
    if (err != ESP_OK) goto done;
    const quality_ab_roi_t marker = quality_ab_marker(target.u8g2);
    solar_os_shell_io_printf(io,
        "AB begin batch=%s waveform=%s width=800 height=480 spi_khz=40000 path=tile "
        "font=default-r-20 max_fast=10 spacing_ms=500\n", retention ? "B" : "A", argv[4]);
    solar_os_shell_io_printf(io,
        "AB ROI marker=%u,%u,%u,%u near_black=%u,%u,48,48 far_black=32,%u,48,48 "
        "white=32,216,48,24 text=32,32,600,64 thin=32,240,736,1 thick=32,264,736,3\n",
        marker.x, marker.y, marker.w, marker.h, marker.x - 80, marker.y, marker.y);
    quality_ab_white(target.u8g2);
    err = solar_os_display_set_controller_mode(target.name, "refresh=cleanup");
    if (err == ESP_OK) err = quality_ab_present(io, &target, d, "white", 0, false);
    if (err != ESP_OK) { healthy = false; goto done; }
    if (!quality_ab_gate(io, "white")) goto done;
    quality_ab_image(target.u8g2);
    solar_os_shell_io_printf(io, "AB image bytes=%u native_fnv1a=%08lx\n",
                             (unsigned)d->buffer_size,
                             (unsigned long)quality_ab_image_hash(d->buffer, d->buffer_size));
    if (retention) {
        err = solar_os_display_set_controller_mode(target.name, "refresh=cleanup");
        if (err == ESP_OK) err = quality_ab_present(io, &target, d, "initial-otp", 0, false);
        if (err != ESP_OK) { healthy = false; goto done; }
        if (!quality_ab_gate(io, "initial-otp")) goto done;
    }
    char mode[40];
    snprintf(mode, sizeof(mode), "fastest-waveform=%s", argv[4]);
    err = solar_os_display_set_controller_mode(target.name, mode);
    if (err != ESP_OK) goto done;
    if (!retention) {
        err = quality_ab_present(io, &target, d, "initial-fast", ++fast_count, true);
        if (err != ESP_OK) { healthy = false; goto done; }
        if (!quality_ab_gate(io, "initial-fast")) goto done;
    }
    /* Derive native bit masks with the current U8g2 rotation. These temporary
     * draws are never submitted. All initial black bits are fixed; the marker
     * starts white and occupies a disjoint region. */
    uint8_t *fixed_black = masks, *marker_mask = masks + d->buffer_size;
    for (size_t i = 0; i < d->buffer_size; ++i) fixed_black[i] = (uint8_t)~d->buffer[i];
    quality_ab_draw_marker(target.u8g2, 1);
    for (size_t i = 0; i < d->buffer_size; ++i)
        marker_mask[i] = (uint8_t)(d->buffer[i] ^ (uint8_t)~fixed_black[i]);
    quality_ab_draw_marker(target.u8g2, 0);
    unsigned marker_bits = 0;
    for (size_t i = 0; i < d->buffer_size; ++i) {
        if (fixed_black[i] & marker_mask[i]) { err = ESP_ERR_INVALID_STATE; goto done; }
        marker_bits += (unsigned)__builtin_popcount((unsigned)marker_mask[i]);
    }
    if (marker_bits != 256) { err = ESP_ERR_INVALID_STATE; goto done; }
    for (unsigned step = 1; fast_count < 10; ++step) {
        quality_ab_draw_marker(target.u8g2, step);
        unsigned changed = 0, fixed_bits = 0, outside = 0;
        bool black_ok = true, white_ok = true;
        for (size_t i = 0; i < d->buffer_size; ++i) {
            const uint8_t old = d->shadow[i], now = d->buffer[i];
            const uint8_t delta = old ^ now;
            const uint8_t white = (uint8_t)~(fixed_black[i] | marker_mask[i]);
            if ((old | now) & fixed_black[i]) black_ok = false;
            if ((old & now & white) != white) white_ok = false;
            outside += (unsigned)__builtin_popcount((unsigned)(delta & (uint8_t)~marker_mask[i]));
            changed += (unsigned)__builtin_popcount((unsigned)delta);
            fixed_bits += (unsigned)__builtin_popcount((unsigned)fixed_black[i]);
        }
        solar_os_shell_io_printf(io,
            "AB invariant step=%u fixed_black_00=%u fixed_white_11=%u fixed_bits=%u outside_changes=%u "
            "changed_pixels=%u\n", step, black_ok, white_ok, fixed_bits, outside, changed);
        if (!black_ok || !white_ok || outside || changed != 256) { err = ESP_ERR_INVALID_STATE; goto done; }
        err = quality_ab_present(io, &target, d, "marker", ++fast_count, true);
        if (err != ESP_OK) { healthy = false; goto done; }
        vTaskDelay(pdMS_TO_TICKS(500));
        if (fast_count < 10 && !quality_ab_gate(io, "marker")) goto done;
    }
    solar_os_shell_io_printf(io, "AB finished fast_count=%u: inspect before cleanup\n", fast_count);
    (void)quality_ab_gate(io, "finished");
done:
    solar_os_shell_io_printf(io, "AB end err=%d fast_count=%u healthy=%u\n", err, fast_count, healthy);
    if (configured) {
        /* Cleanup only on a healthy exit; hardware failure is never retried.
         * Restore the pre-trial RAM selector and refresh policy after cleanup. */
        if (healthy) {
            const esp_err_t cleanup = solar_os_display_set_controller_mode(target.name, "refresh=cleanup");
            if (cleanup == ESP_OK) (void)quality_ab_present(io, &target, d, "exit-cleanup", 0, false);
        }
        char restore[40];
        const char *names[] = {"baseline", "dose", "hold"};
        snprintf(restore, sizeof(restore), "fastest-waveform=%s", names[original_waveform]);
        (void)solar_os_display_set_controller_mode(target.name, restore);
        const char *refresh[] = {"refresh=auto", "refresh=partial", "refresh=full", "refresh=fastest", "refresh=typing"};
        (void)solar_os_display_set_controller_mode(target.name, refresh[original_refresh]);
        (void)solar_os_display_set_controller_mode(target.name, original_timing ? "timing=on" : "timing=off");
    }
    solar_os_memory_free(masks);
    (void)solar_os_display_release(target.name, owner);
}
