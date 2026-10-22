/* PaperMono's opt-in refresh=fastest backend. AUTO retains the OTP policy.
 * LUT from the user's installed M5GFX Panel_SSD1677.cpp (hash/review in
 * doc/m5paper-mono-writer-strategy.md). Copyright (c) 2021 M5Stack, MIT;
 * see m5paper_mono.LICENSE. Original LovyanGFX attribution is retained in
 * epd_ssd1677_reference.LICENSE. No claim of rectangular physical activation.
 *
 * Mono specialization of the reference transition encoding: BW=new, RED=old.
 * Both full RAM banks are overwritten each time, so Mode-2 ping-pong never
 * exposes stale transition classes, and no host plane swap is needed. */
static const uint8_t ssd1677_reference_lut[110] = {
    0,0,0,0,0,0,0,0,0,0,
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

#if SOLAR_OS_SSD1677_WAVEFORM_TRIAL
#include "epd_ssd1677_waveform_trial.inc"
#endif

#if SOLAR_OS_SSD1677_WRITER_D
static const uint8_t ssd1677_trial_black_hold_d_lut[110] = {
    0x2f,0xf0,0,0,0,0,0,0,0,0,
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

/* E1 is RAM-only opt-in, including Sketch. Only LUT0 bytes 0..1 differ
 * from A: 0V*2F, +5V*2F, -15V*2F, +5V*4F (10F). Source sum zero
 * is not a claim of effective pixel DC balance or long-term panel safety. */
static const uint8_t ssd1677_trial_black_hold_e1_lut[110] = {
    0x3b,0xf0,0,0,0,0,0,0,0,0,
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

/* Explicit, volatile Candidate F: existing VSH2 (+5V) for Phase 1A only.
 * TP/RP/FR and all other LUT/voltage bytes remain Baseline A's. Source
 * 0V*8F,+5V*1F,0V*1F is not proof of effective DC balance or panel safety. */
static const uint8_t ssd1677_trial_black_hold_f_lut[110] = {
    0x00,0xc0,0,0,0,0,0,0,0,0,
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
#endif

static const uint8_t *ssd1677_selected_lut(const epd_ssd1677_t *d, const char **name)
{
    *name = "baseline";
#if SOLAR_OS_SSD1677_WAVEFORM_TRIAL
    /* Typing is a separate, existing experiment; do not change its waveform. */
    if (d->refresh_mode == EPD_SSD1677_REFRESH_FASTEST) {
        if (d->waveform_trial == EPD_SSD1677_WAVEFORM_DOSE) {
            *name = "dose";
            return ssd1677_trial_dose_lut;
        }
        if (d->waveform_trial == EPD_SSD1677_WAVEFORM_HOLD) {
            *name = "hold";
            return ssd1677_trial_hold_lut;
        }
    }
#else
    (void)d;
#endif
    return ssd1677_reference_lut;
}

/* Byte loads are alignment- and endian-independent. Transpose column-major
 * U8g2 bits into MSB-first controller rows; no rotation beyond the old mapper. */
static void ssd1677_transpose_tile(const uint8_t *columns, uint8_t *rows, size_t stride)
{
    uint64_t x = 0;
    for (unsigned i = 0; i < 8; ++i) x |= (uint64_t)columns[i] << ((7U-i)*8U);
    uint64_t t = (x ^ (x >> 7)) & UINT64_C(0x00aa00aa00aa00aa);
    x ^= t ^ (t << 7);
    t = (x ^ (x >> 14)) & UINT64_C(0x0000cccc0000cccc);
    x ^= t ^ (t << 14);
    t = (x ^ (x >> 28)) & UINT64_C(0x00000000f0f0f0f0);
    x ^= t ^ (t << 28);
    for (unsigned y = 0; y < 8; ++y) rows[y*stride] = (uint8_t)(x >> (y*8U));
}

static bool ssd1677_find_change_bytes(const epd_ssd1677_t *d, ssd1677_window_t *w)
{
    const uint8_t *frame = ssd1677_frame(d);
    bool changed = false;
    *w = (ssd1677_window_t){SSD1677_PANEL_ROW_BYTES, 0, SSD1677_HEIGHT, 0};
    for (unsigned tile_y = 0; tile_y < SSD1677_TILE_HEIGHT; ++tile_y) {
        const size_t row = tile_y*SSD1677_BUFFER_ROW_BYTES;
        for (unsigned x = 0; x < SSD1677_WIDTH; ++x) {
            const unsigned bits = frame[row+x] ^ d->shadow[row+x];
            if (!bits) continue;
            const unsigned first = (unsigned)__builtin_ctz(bits);
            const unsigned last = 31U-(unsigned)__builtin_clz(bits);
            const unsigned xb = x/8, top = tile_y*8+first, bottom = tile_y*8+last;
            if (xb < w->x_start_byte) w->x_start_byte = xb;
            if (xb > w->x_end_byte) w->x_end_byte = xb;
            if (top < w->y_start) w->y_start = top;
            if (bottom > w->y_end) w->y_end = bottom;
            changed = true;
        }
    }
    return changed;
}

static esp_err_t ssd1677_write_bands(epd_ssd1677_t *d, uint8_t command,
                                   const uint8_t *source, unsigned phase)
{
    const ssd1677_window_t full = {0, SSD1677_PANEL_ROW_BYTES-1, 0, SSD1677_HEIGHT-1};
    ESP_RETURN_ON_ERROR(ssd1677_set_partial_window(d, &full), TAG, "band RAM address failed");
    ESP_RETURN_ON_ERROR(ssd1677_cmd(d, command), TAG, "band RAM write failed");
    ESP_RETURN_ON_ERROR(gpio_set_level((gpio_num_t)d->dc_pin, 1), TAG, "band D/C failed");
    for (unsigned y = 0; y < SSD1677_HEIGHT; y += SSD1677_BAND_ROWS) {
        const int64_t convert_start = esp_timer_get_time();
        for (unsigned row = 0; row < SSD1677_BAND_ROWS; row += 8) {
            const uint8_t *tile_row = source + ((y+row)/8)*SSD1677_BUFFER_ROW_BYTES;
            for (unsigned byte = 0; byte < SSD1677_PANEL_ROW_BYTES; ++byte)
                ssd1677_transpose_tile(tile_row+byte*8,
                                      d->band_buffer+row*SSD1677_PANEL_ROW_BYTES+byte,
                                      SSD1677_PANEL_ROW_BYTES);
        }
        const int64_t convert_end = esp_timer_get_time();
        if (y == 0) d->timing.convert_start_us[phase] = convert_start;
        d->timing.convert_end_us[phase] = convert_end;
        d->timing.convert_us[phase] += convert_end-convert_start;
        spi_transaction_t transaction = {.length = SSD1677_BAND_BYTES*8, .tx_buffer = d->band_buffer};
        const esp_err_t err = ssd1677_poll(d, &transaction);
        d->timing.spi_us[phase] += esp_timer_get_time()-convert_end;
        ESP_RETURN_ON_ERROR(err, TAG, "band transmit failed");
    }
    return ESP_OK;
}

#if SOLAR_OS_SSD1677_DIRTY_HOLD_TRIAL
/* The input remains SolarOS tile-major; only the addressed controller rows
 * are transposed and transmitted. This never touches pixels outside w. */
static esp_err_t ssd1677_write_tile_window(epd_ssd1677_t *d, uint8_t command,
                                           const uint8_t *source,
                                           const ssd1677_window_t *w, unsigned phase)
{
    if (!d || !d->band_buffer || !source || !w ||
        w->x_start_byte > w->x_end_byte || w->x_end_byte >= SSD1677_PANEL_ROW_BYTES ||
        w->y_start > w->y_end || w->y_end >= SSD1677_HEIGHT || phase >= 4U)
        return ESP_ERR_INVALID_ARG;
    const size_t row_bytes = w->x_end_byte - w->x_start_byte + 1U;
    ESP_RETURN_ON_ERROR(ssd1677_set_partial_window(d, w), TAG, "dirty RAM address failed");
    ESP_RETURN_ON_ERROR(ssd1677_cmd(d, command), TAG, "dirty RAM write failed");
    ESP_RETURN_ON_ERROR(gpio_set_level((gpio_num_t)d->dc_pin, 1), TAG, "dirty D/C failed");
    for (unsigned tile_y = w->y_start / 8U; tile_y <= w->y_end / 8U; ++tile_y) {
        const int64_t convert_start = esp_timer_get_time();
        const uint8_t *tile_row = source + tile_y * SSD1677_BUFFER_ROW_BYTES;
        for (unsigned byte = w->x_start_byte; byte <= w->x_end_byte; ++byte) {
            ssd1677_transpose_tile(tile_row + byte * 8U,
                                   d->band_buffer + byte - w->x_start_byte,
                                   row_bytes);
        }
        const int64_t convert_end = esp_timer_get_time();
        if (tile_y == w->y_start / 8U) d->timing.convert_start_us[phase] = convert_start;
        d->timing.convert_end_us[phase] = convert_end;
        d->timing.convert_us[phase] += convert_end - convert_start;
        const unsigned first = tile_y * 8U < w->y_start ? w->y_start - tile_y * 8U : 0U;
        const unsigned last = tile_y * 8U + 7U > w->y_end ? w->y_end - tile_y * 8U : 7U;
        spi_transaction_t transaction = {
            .length = (last - first + 1U) * row_bytes * 8U,
            .tx_buffer = d->band_buffer + first * row_bytes,
        };
        const esp_err_t err = ssd1677_poll(d, &transaction);
        d->timing.spi_us[phase] += esp_timer_get_time() - convert_end;
        ESP_RETURN_ON_ERROR(err, TAG, "dirty transmit failed");
    }
    return ESP_OK;
}
#endif

static esp_err_t ssd1677_reference_refresh(epd_ssd1677_t *d)
{
    const int64_t start = esp_timer_get_time();
    ssd1677_window_t dirty;
    const bool changed = d->fastest_tiles ? ssd1677_find_change_bytes(d, &dirty) :
                                           ssd1677_find_change_window(d, &dirty);
    if (!changed) return ESP_OK;
    const int64_t diff_end = esp_timer_get_time();
    d->transport_spi_hz = d->fastest_spi_hz;
    const ssd1677_window_t full = {0, SSD1677_PANEL_ROW_BYTES-1, 0, SSD1677_HEIGHT-1};
    d->timing = (epd_ssd1677_timing_t){.start_us=start, .partial=true, .diff_us=diff_end-start};
#if SOLAR_OS_SSD1677_WRITER_D
    d->writer_trial.current.timing_valid = true;
#endif
    d->papermono_baseline_valid = false;
    const bool cold = !d->reference_awake;
#if SOLAR_OS_SSD1677_DIRTY_HOLD_TRIAL
    if (d->dirty_hold_trial && d->dirty_hold_dual_sync &&
        d->dirty_hold_dual_updates >= 32U) {
#if SOLAR_OS_SSD1677_WRITER_D
        d->writer_trial.current.periodic_resync = true;
#endif
        d->dirty_hold_ram_synced = false;
        d->dirty_hold_dual_updates = 0;
        ESP_LOGI(TAG, "Dirty RAM dual: periodic full-RAM resync after 32 updates");
    }
    const bool trial = d->dirty_hold_trial &&
                       d->refresh_mode == EPD_SSD1677_REFRESH_FASTEST &&
                       d->fastest_tiles && d->band_buffer != NULL;
    const bool synchronized = trial && !cold && d->dirty_hold_ram_synced;
    const bool narrow = synchronized && !d->dirty_hold_force_full;
    ssd1677_window_t ram_window = dirty;
    if (narrow && d->dirty_hold_full_height) {
        ram_window.y_start = 0;
        ram_window.y_end = SSD1677_HEIGHT - 1U;
    }
    if (narrow && d->dirty_hold_full_width) {
        ram_window.x_start_byte = 0;
        ram_window.x_end_byte = SSD1677_PANEL_ROW_BYTES - 1U;
    }
    d->dirty_hold_ram_synced = false; /* Unknown until post-activation sync succeeds. */
#endif
    const char *waveform_name;
    const uint8_t *lut = ssd1677_selected_lut(d, &waveform_name);
#if SOLAR_OS_SSD1677_WRITER_D
    if (writer_d_select(d, cold)) {
        lut = d->writer_trial.used_f ? ssd1677_trial_black_hold_f_lut :
              d->writer_trial.used_e1 ? ssd1677_trial_black_hold_e1_lut : ssd1677_trial_black_hold_d_lut;
        waveform_name = d->writer_trial.used_f ? "candidate_f" :
                        d->writer_trial.used_e1 ? "candidate_e1" : "candidate_d";
    }
    {
        epd_ssd1677_writer_record_t *r = &d->writer_trial.current;
#if SOLAR_OS_SSD1677_DIRTY_HOLD_TRIAL
        const ssd1677_window_t *w = narrow ? &ram_window : &full;
        r->ram_path = narrow ? (d->dirty_hold_full_width ? SSD1677_RAM_ROWS : SSD1677_RAM_RECT) : SSD1677_RAM_FULL;
#else
        const ssd1677_window_t *w = &full;
        r->ram_path = SSD1677_RAM_FULL;
#endif
        r->ram_x = w->x_start_byte * 8U;
        r->ram_y = w->y_start;
        r->ram_width = (w->x_end_byte - w->x_start_byte + 1U) * 8U;
        r->ram_height = w->y_end - w->y_start + 1U;
    }
    { /* Record all app updates; the source never gates compensation. */
        d->writer_trial.current.x = dirty.x_start_byte * 8;
        d->writer_trial.current.y = dirty.y_start;
        d->writer_trial.current.width = (dirty.x_end_byte - dirty.x_start_byte + 1) * 8;
        d->writer_trial.current.height = dirty.y_end - dirty.y_start + 1;
    }
#endif
    esp_err_t err =
#if SOLAR_OS_SSD1677_WRITER_D
        d->faulted ? ESP_ERR_INVALID_STATE :
#endif
        ESP_OK;
    if (cold && err == ESP_OK) {
        /* Reset electronic state, not the optical completed-shadow baseline. */
        err = ssd1677_controller_init(d);
        const uint8_t booster[] = {0xae,0xc7,0xc3,0xc0,0x40};
        if (err == ESP_OK) err = ssd1677_cmd_data(d,0x0c,booster,sizeof(booster));
    }
#if SOLAR_OS_SSD1677_DIRTY_HOLD_TRIAL
    const unsigned pre_writes = synchronized ? 1U : 2U;
#else
    const unsigned pre_writes = 2U;
#endif
    for (unsigned i=0; i<pre_writes && err==ESP_OK; ++i) {
        d->timing.writes++;
        d->timing.write_start_us[i] = esp_timer_get_time();
        const uint8_t *source = i ? d->shadow : ssd1677_frame(d);
#if SOLAR_OS_SSD1677_DIRTY_HOLD_TRIAL
        /* A synchronized RED plane already holds the old image, even when
         * the new-image window is forced full. Cold/unsynchronized entry
         * still prepares both full planes before activation. */
        err = narrow ? ssd1677_write_tile_window(d, 0x24, source, &ram_window, i) :
              d->fastest_tiles ? ssd1677_write_bands(d, i ? 0x26 : 0x24, source, i) :
                                 ssd1677_write_plane(d, i ? 0x26 : 0x24, source, &full, false);
#else
        err = d->fastest_tiles ? ssd1677_write_bands(d, i ? 0x26 : 0x24, source, i) :
                                ssd1677_write_plane(d, i ? 0x26 : 0x24, source, &full, false);
#endif
        if (err == ESP_OK) {
#if SOLAR_OS_SSD1677_DIRTY_HOLD_TRIAL
            d->timing.ram_data_bytes[i] = narrow ?
                (ram_window.x_end_byte - ram_window.x_start_byte + 1U) *
                (ram_window.y_end - ram_window.y_start + 1U) : SSD1677_BUFFER_BYTES;
#else
            d->timing.ram_data_bytes[i] = SSD1677_BUFFER_BYTES;
#endif
        }
        d->timing.write_end_us[i] = esp_timer_get_time();
    }
#if SOLAR_OS_SSD1677_WRITER_D
    const int64_t lut_start = esp_timer_get_time();
#endif
    if (err == ESP_OK) err = ssd1677_cmd_data(d,0x32,lut,105);
#if SOLAR_OS_SSD1677_WRITER_D
    d->writer_trial.current.lut_us = esp_timer_get_time() - lut_start;
#endif
    if (err == ESP_OK) err = ssd1677_cmd_data(d,0x03,lut+105,1);
    if (err == ESP_OK) err = ssd1677_cmd_data(d,0x04,lut+106,3);
    if (err == ESP_OK) err = ssd1677_cmd_data(d,0x2c,lut+109,1);
    const uint8_t ctrl1=0, ctrl2=cold ? 0xcc : 0x0c;
    if (err == ESP_OK) err = ssd1677_cmd_data(d,0x21,&ctrl1,1);
    if (err == ESP_OK) err = ssd1677_cmd_data(d,0x22,&ctrl2,1);
    if (err == ESP_OK) {
        d->timing.activations=1;
        d->timing.activate_us[0]=esp_timer_get_time();
        err=ssd1677_cmd(d,0x20);
        if (err == ESP_OK) {
            /* At least one RTOS tick: avoid mistaking pre-assertion LOW for done.
             * Reference's guard is 2ms; SolarOS tick may be 10ms. */
            vTaskDelay(1);
            const int64_t deadline=esp_timer_get_time()+10000000;
            while (gpio_get_level((gpio_num_t)d->busy_pin)==d->busy_level) {
                if (esp_timer_get_time()>=deadline) { err=ESP_ERR_TIMEOUT; break; }
                vTaskDelay(1);
            }
        }
        d->timing.ready_us[0]=esp_timer_get_time();
    }
#if SOLAR_OS_SSD1677_DIRTY_HOLD_TRIAL
    if (err == ESP_OK && trial) {
        /* Mode-2 may exchange the command-addressed RAM faces on activation.
         * A single 0x26 write would then leave the prior image in 0x24.
         * The opt-in trial synchronizes both faces before the next update. */
        const unsigned sync_count = d->dirty_hold_dual_sync ? 2U : 1U;
        for (unsigned sync = 0; sync < sync_count && err == ESP_OK; ++sync) {
            const uint8_t command = d->dirty_hold_dual_sync && sync == 0 ? 0x24 : 0x26;
            const unsigned i = d->timing.writes++;
            d->timing.write_start_us[i] = esp_timer_get_time();
            err = narrow ? ssd1677_write_tile_window(d, command, ssd1677_frame(d), &ram_window, i) :
                           ssd1677_write_bands(d, command, ssd1677_frame(d), i);
            if (err == ESP_OK) d->timing.ram_data_bytes[i] = narrow ?
                (ram_window.x_end_byte - ram_window.x_start_byte + 1U) *
                (ram_window.y_end - ram_window.y_start + 1U) : SSD1677_BUFFER_BYTES;
            d->timing.write_end_us[i] = esp_timer_get_time();
        }
        if (err == ESP_OK) d->dirty_hold_ram_synced = true;
    }
#endif
#if SOLAR_OS_SSD1677_WRITER_D
    if (err == ESP_OK && (d->writer_trial.used_d || d->writer_trial.used_e1 || d->writer_trial.used_f)) {
        const int64_t restore_start = esp_timer_get_time();
        /* Existing D trial restore: after BUSY AND both RAM syncs. No activation. */
        err = ssd1677_cmd_data(d, 0x32, ssd1677_reference_lut, 105);
        d->writer_trial.current.restore_us = esp_timer_get_time() - restore_start;
        d->writer_trial.restored = err == ESP_OK;
    }
#endif
    if (err == ESP_OK) {
        const int64_t promote_start = esp_timer_get_time();
        memcpy(d->shadow,ssd1677_frame(d),d->buffer_size);
        d->timing.promote_us = esp_timer_get_time()-promote_start;
        d->shadow_valid=true;
        d->papermono_baseline_valid=true;
        d->reference_awake=true;
        d->controller_ready=true;
        d->partial_refresh_active=true;
#if SOLAR_OS_SSD1677_DIRTY_HOLD_TRIAL
        if (trial && d->dirty_hold_dual_sync) ++d->dirty_hold_dual_updates;
#endif
    } else {
        d->reference_awake=false;
        d->controller_ready=false;
        d->shadow_valid=false;
        d->faulted=true;
    }
    d->timing.end_us=esp_timer_get_time();
    if (d->timing_enabled || err!=ESP_OK) {
        ESP_LOGI(TAG,"REF err=%d cold=%u region=%u,%u,%u,%u ram_bytes=%u 0x20=%u start=%" PRId64
                 " end=%" PRId64 " total_us=%" PRId64 " busy_us=%" PRId64,
                 (int)err,cold,dirty.x_start_byte*8,dirty.y_start,
                 (dirty.x_end_byte-dirty.x_start_byte+1)*8,dirty.y_end-dirty.y_start+1,
                 (unsigned)(d->timing.ram_data_bytes[0] + d->timing.ram_data_bytes[1] +
                            d->timing.ram_data_bytes[2] + d->timing.ram_data_bytes[3]),
                 (unsigned)d->timing.activations,
                 start,d->timing.end_us,d->timing.end_us-start,
                 d->timing.ready_us[0]-d->timing.activate_us[0]);
        ESP_LOGI(TAG, "REF phases path=%s spi_khz=%d diff_us=%" PRId64 " promote_us=%" PRId64 " waveform=%s",
                 d->fastest_tiles ? "tile" : "scalar", d->actual_spi_hz/1000,
                 d->timing.diff_us, d->timing.promote_us, waveform_name);
        for (unsigned i=0;i<d->timing.writes;i++)
            ESP_LOGI(TAG,"REF write%u ram_bytes=%u us=%" PRId64 " convert_us=%" PRId64 " spi_us=%" PRId64,
                     i,(unsigned)d->timing.ram_data_bytes[i],
                     d->timing.write_end_us[i]-d->timing.write_start_us[i],
                     d->timing.convert_us[i],d->timing.spi_us[i]);
    }
    return err;
}
