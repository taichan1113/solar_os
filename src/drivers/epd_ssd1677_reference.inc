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

/* Unmodified M5GFX lut_fast_SSD1677: 48-frame absolute Mode-2 waveform.
 * Mono endpoints use group 0=white / 3=black. Both RAM planes contain the
 * inverted CURRENT image, unlike fastest's current/previous transition planes.
 * Source revision and byte comparison: doc/m5paper-mono-text-contrast.md.
 * Copyright (c) M5Stack; existing MIT/LovyanGFX attribution applies. */
static const uint8_t ssd1677_balanced_lut[110] = {
    0x55,0x55,0x55,0x55,0x55,0x5a,0xaa,0xaa,0,0,
    0xaa,0x95,0x55,0x55,0x55,0x5a,0x82,0xac,0,0,
    0xaa,0xa5,0x55,0x55,0x55,0x5a,0xac,0,0,0,
    0xaa,0xaa,0xaa,0xaa,0x55,0x55,0x55,0x50,0,0,
    0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,0, 1,1,1,1,0, 1,1,1,1,0, 1,1,1,1,0,
    2,2,2,2,0, 2,2,2,2,0, 2,2,2,2,0, 2,2,2,2,0,
    0,0,0,0,0, 0,0,0,0,0,
    0x8f,0x8f,0x8f,0x8f,0x8f,
    0x17,0x41,0xa8,0x32,0x30,
};

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
                                   const uint8_t *source, unsigned phase, bool invert)
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
        if (invert) {
            for (unsigned byte = 0; byte < SSD1677_BAND_BYTES; ++byte)
                d->band_buffer[byte] = (uint8_t)~d->band_buffer[byte];
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

static esp_err_t ssd1677_reference_refresh(epd_ssd1677_t *d)
{
    const int64_t start = esp_timer_get_time();
    ssd1677_window_t dirty;
    const bool changed = d->fastest_tiles ? ssd1677_find_change_bytes(d, &dirty) :
                                           ssd1677_find_change_window(d, &dirty);
    if (!changed) return ESP_OK;
    const int64_t diff_end = esp_timer_get_time();
    const bool balanced = d->refresh_mode == EPD_SSD1677_REFRESH_BALANCED;
    const uint8_t *lut = balanced ? ssd1677_balanced_lut : ssd1677_reference_lut;
    d->transport_spi_hz = d->fastest_spi_hz;
    const ssd1677_window_t full = {0, SSD1677_PANEL_ROW_BYTES-1, 0, SSD1677_HEIGHT-1};
    d->timing = (epd_ssd1677_timing_t){.start_us=start, .partial=true, .diff_us=diff_end-start};
    d->papermono_baseline_valid = false;
    const bool cold = !d->reference_awake;
    esp_err_t err = ESP_OK;
    if (cold) {
        /* Reset electronic state, not the optical completed-shadow baseline. */
        err = ssd1677_controller_init(d);
        const uint8_t booster[] = {0xae,0xc7,0xc3,0xc0,0x40};
        if (err == ESP_OK) err = ssd1677_cmd_data(d,0x0c,booster,sizeof(booster));
    }
    for (unsigned i=0; i<2 && err==ESP_OK; ++i) {
        d->timing.writes++;
        d->timing.write_start_us[i] = esp_timer_get_time();
        const uint8_t *source = !i || balanced ? ssd1677_frame(d) : d->shadow;
        err = d->fastest_tiles ? ssd1677_write_bands(d, i ? 0x26 : 0x24, source, i, balanced) :
                                ssd1677_write_plane(d, i ? 0x26 : 0x24, source, &full, balanced);
        d->timing.write_end_us[i] = esp_timer_get_time();
    }
    if (err == ESP_OK) err = ssd1677_cmd_data(d,0x32,lut,105);
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
    if (err == ESP_OK) {
        const int64_t promote_start = esp_timer_get_time();
        memcpy(d->shadow,ssd1677_frame(d),d->buffer_size);
        d->timing.promote_us = esp_timer_get_time()-promote_start;
        d->shadow_valid=true;
        d->papermono_baseline_valid=true;
        d->reference_awake=true;
        d->controller_ready=true;
        d->partial_refresh_active=true;
    } else {
        d->reference_awake=false;
        d->controller_ready=false;
        d->shadow_valid=false;
        d->faulted=true;
    }
    d->timing.end_us=esp_timer_get_time();
    if (d->timing_enabled || err!=ESP_OK) {
        ESP_LOGI(TAG,"REF err=%d cold=%u region=%u,%u,%u,%u bytes=96000 start=%" PRId64
                 " end=%" PRId64 " total_us=%" PRId64 " busy_us=%" PRId64,
                 (int)err,cold,dirty.x_start_byte*8,dirty.y_start,
                 (dirty.x_end_byte-dirty.x_start_byte+1)*8,dirty.y_end-dirty.y_start+1,
                 start,d->timing.end_us,d->timing.end_us-start,
                 d->timing.ready_us[0]-d->timing.activate_us[0]);
        ESP_LOGI(TAG, "REF phases path=%s spi_khz=%d diff_us=%" PRId64 " promote_us=%" PRId64 " profile=%s",
                 d->fastest_tiles ? "tile" : "scalar", d->actual_spi_hz/1000,
                 d->timing.diff_us, d->timing.promote_us, balanced ? "balanced" : "fastest");
        for (unsigned i=0;i<d->timing.writes;i++)
            ESP_LOGI(TAG,"REF write%u us=%" PRId64 " convert_us=%" PRId64 " spi_us=%" PRId64,
                     i,d->timing.write_end_us[i]-d->timing.write_start_us[i],
                     d->timing.convert_us[i],d->timing.spi_us[i]);
    }
    return err;
}
