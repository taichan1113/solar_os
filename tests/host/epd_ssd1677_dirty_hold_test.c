/* Reuse the real Driver and SPI capture from the established SSD1677 fixture. */
#define main epd_ssd1677_baseline_main
#include "epd_ssd1677_test.c"
#undef main
#include <time.h>

#if !SOLAR_OS_SSD1677_DIRTY_HOLD_TRIAL
#error This test requires SOLAR_OS_SSD1677_DIRTY_HOLD_TRIAL=1
#endif

typedef struct {
    uint8_t bw[SSD1677_PANEL_ROW_BYTES * SSD1677_HEIGHT];
    uint8_t red[SSD1677_PANEL_ROW_BYTES * SSD1677_HEIGHT];
    uint8_t visible[SSD1677_PANEL_ROW_BYTES * SSD1677_HEIGHT];
    uint8_t bw_known[SSD1677_PANEL_ROW_BYTES * SSD1677_HEIGHT];
    uint8_t red_known[SSD1677_PANEL_ROW_BYTES * SSD1677_HEIGHT];
    unsigned xs, xe, ys, ye, cx, cy;
    unsigned activations;
    uint8_t update_ctrl2;
    unsigned mode2_face; /* Logical activation parity, not a physical RAM bank. */
    unsigned mode2_face_visits[2];
    size_t classes[4]; /* old/new: BB, BW, WB, WW */
} controller_ram_t;

typedef struct {
    size_t bw_bytes, red_bytes, spi_bytes, converted_bytes;
    unsigned activations;
    double host_driver_ms;
} transfer_t;

static void panel_frame(epd_ssd1677_t *d, uint8_t *out)
{
    for (unsigned y = 0; y < SSD1677_HEIGHT; ++y) {
        ssd1677_convert_row(d, ssd1677_frame(d), (uint16_t)y);
        memcpy(out + y * SSD1677_PANEL_ROW_BYTES, line, SSD1677_PANEL_ROW_BYTES);
    }
}

static void controller_seed(controller_ram_t *m, epd_ssd1677_t *d)
{
    memset(m, 0, sizeof(*m));
    panel_frame(d, m->visible);
    memcpy(m->bw, m->visible, sizeof(m->bw));
    memcpy(m->red, m->visible, sizeof(m->red));
    memset(m->bw_known, 1, sizeof(m->bw_known));
    memset(m->red_known, 1, sizeof(m->red_known));
}

static void controller_write(controller_ram_t *m, const captured_command_t *c)
{
    assert(m->xs <= m->xe && m->xe < SSD1677_WIDTH);
    assert(m->ys >= m->ye && m->ys < SSD1677_HEIGHT);
    assert((m->xs & 7U) == 0 && (m->xe & 7U) == 7U);
    assert(m->cx >= m->xs && m->cx <= m->xe && m->cy <= m->ys && m->cy >= m->ye);
    const unsigned first = m->xs / 8U, last = m->xe / 8U;
    unsigned xb = m->cx / 8U, y = m->cy;
    uint8_t *bank = c->command == 0x24 ? m->bw : m->red;
    uint8_t *known = c->command == 0x24 ? m->bw_known : m->red_known;
    for (size_t i = 0; i < c->length; ++i) {
        assert(y >= m->ye && y < SSD1677_HEIGHT && xb <= last);
        const size_t at = (SSD1677_HEIGHT - 1U - y) * SSD1677_PANEL_ROW_BYTES + xb;
        bank[at] = c->data[i];
        known[at] = 1;
        if (++xb > last) {
            xb = first;
            if (i + 1U < c->length) assert(y > m->ye);
            if (y > 0) --y;
        }
    }
}

static transfer_t controller_apply(controller_ram_t *m, epd_ssd1677_t *d)
{
    uint8_t target[sizeof(m->visible)];
    panel_frame(d, target);
    transfer_t result = {0};
    for (size_t n = 0; n < command_count; ++n) {
        const captured_command_t *c = &commands[n];
        result.spi_bytes += 1U + c->length;
        switch (c->command) {
        case 0x12:
            memset(m->bw_known, 0, sizeof(m->bw_known));
            memset(m->red_known, 0, sizeof(m->red_known));
            m->mode2_face = 0;
            break;
        case 0x22:
            assert(c->length == 1);
            m->update_ctrl2 = c->data[0];
            break;
        case 0x44:
            assert(c->length == 4);
            m->xs = c->data[0] | ((unsigned)c->data[1] << 8);
            m->xe = c->data[2] | ((unsigned)c->data[3] << 8);
            break;
        case 0x45:
            assert(c->length == 4);
            m->ys = c->data[0] | ((unsigned)c->data[1] << 8);
            m->ye = c->data[2] | ((unsigned)c->data[3] << 8);
            break;
        case 0x4e:
            assert(c->length == 2);
            m->cx = c->data[0] | ((unsigned)c->data[1] << 8);
            break;
        case 0x4f:
            assert(c->length == 2);
            m->cy = c->data[0] | ((unsigned)c->data[1] << 8);
            break;
        case 0x24:
        case 0x26:
            if (c->command == 0x24) result.bw_bytes += c->length;
            else result.red_bytes += c->length;
            result.converted_bytes +=
                ((SSD1677_HEIGHT - 1U - m->ye) / 8U -
                 (SSD1677_HEIGHT - 1U - m->ys) / 8U + 1U) * 8U *
                (m->xe / 8U - m->xs / 8U + 1U);
            controller_write(m, c);
            break;
        case 0x20:
            assert((m->update_ctrl2 & 0x0cU) == 0x0cU);
            ++m->mode2_face_visits[m->mode2_face];
            ++result.activations;
            ++m->activations;
            for (size_t i = 0; i < sizeof(target); ++i) {
                assert(m->bw_known[i] && m->red_known[i]);
                assert(m->bw[i] == target[i]);
                assert(m->red[i] == m->visible[i]);
                for (unsigned bit = 0; bit < 8; ++bit) {
                    const unsigned old_white = (m->visible[i] >> bit) & 1U;
                    const unsigned new_white = (target[i] >> bit) & 1U;
                    ++m->classes[old_white * 2U + new_white];
                }
            }
            memcpy(m->visible, m->bw, sizeof(m->visible));
            /* M5GFX tracks this face across Mode-2 activations. The model
             * deliberately leaves the command-addressed 0x24/0x26 RAM
             * contents unchanged here; actual silicon retention is unproven. */
            m->mode2_face ^= 1U;
            break;
        default: break;
        }
    }
    assert(memcmp(m->visible, target, sizeof(target)) == 0);
    return result;
}

static void flip(unsigned x, unsigned y)
{
    assert(x < SSD1677_WIDTH && y < SSD1677_HEIGHT);
    frame[(y / 8U) * SSD1677_BUFFER_ROW_BYTES + x] ^= (uint8_t)(1U << (y & 7U));
}

static double benchmark_bounds_us(void)
{
    epd_ssd1677_t d = make_display();
    memcpy(shadow, frame, sizeof(frame));
    const clock_t start = clock();
    for (unsigned i = 0; i < 500; ++i) {
        flip(i % SSD1677_WIDTH, i % SSD1677_HEIGHT);
        ssd1677_window_t w;
        assert(ssd1677_find_change_bytes(&d, &w));
        assert(w.x_start_byte == (i % SSD1677_WIDTH) / 8U);
        flip(i % SSD1677_WIDTH, i % SSD1677_HEIGHT);
    }
    return 1000000.0 * (clock() - start) / CLOCKS_PER_SEC / 500.0;
}

static transfer_t run_step(epd_ssd1677_t *d, controller_ram_t *m, bool trial,
                           size_t expected_bytes, bool full)
{
    command_count = 0;
    const clock_t start = clock();
    assert(ssd1677_refresh(d) == ESP_OK);
    const clock_t finish = clock();
    const transfer_t t = controller_apply(m, d);
    transfer_t measured = t;
    measured.host_driver_ms = 1000.0 * (finish - start) / CLOCKS_PER_SEC;
    assert(t.activations == 1);
    assert(t.bw_bytes == expected_bytes);
    assert(t.red_bytes == (trial ? expected_bytes * (full ? 2U : 1U) :
                           SSD1677_BUFFER_BYTES));
    assert(memcmp(frame, shadow, sizeof(frame)) == 0);
    if (trial) {
        assert(d->dirty_hold_ram_synced);
        assert(memcmp(m->bw, m->red, sizeof(m->bw)) == 0);
    }
    return measured;
}

static transfer_t run_series(bool trial)
{
    epd_ssd1677_t d = make_display();
    d.papermono_otp = true;
    d.band_buffer = band;
    d.fastest_tiles = true;
    d.dirty_hold_trial = trial;
    assert(epd_ssd1677_set_controller_mode(&d, "refresh=fastest") == ESP_OK);
    assert(ssd1677_refresh(&d) == ESP_OK); /* OTP full baseline. */
    controller_ram_t *m = malloc(sizeof(*m));
    assert(m);
    controller_seed(m, &d);
    transfer_t total = {0};
#define STEP(bytes, fallback) do { \
    const transfer_t s = run_step(&d, m, trial, (bytes), (fallback)); \
    total.bw_bytes += s.bw_bytes; total.red_bytes += s.red_bytes; \
    total.spi_bytes += s.spi_bytes; total.converted_bytes += s.converted_bytes; \
    total.activations += s.activations; \
    total.host_driver_ms += s.host_driver_ms; \
} while (0)
    flip(0, 0); STEP(SSD1677_BUFFER_BYTES, true); /* Cold full. */
    flip(7, 0); flip(8, 0); STEP(trial ? 2 : SSD1677_BUFFER_BYTES, false);
    flip(10, 7); flip(10, 8); STEP(trial ? 2 : SSD1677_BUFFER_BYTES, false);
    flip(799, 0); STEP(trial ? 1 : SSD1677_BUFFER_BYTES, false);
    flip(0, 479); STEP(trial ? 1 : SSD1677_BUFFER_BYTES, false);
    flip(100, 100); flip(700, 400);
    STEP(trial ? (76U * 301U) : SSD1677_BUFFER_BYTES, false); /* One bounding rectangle. */
    flip(100, 100); STEP(trial ? 1 : SSD1677_BUFFER_BYTES, false); /* Black -> white. */
    flip(100, 100); STEP(trial ? 1 : SSD1677_BUFFER_BYTES, false); /* White -> black. */
    flip(8, 0); STEP(trial ? 1 : SSD1677_BUFFER_BYTES, false);
    flip(8, 0); STEP(trial ? 1 : SSD1677_BUFFER_BYTES, false);
#undef STEP
    command_count = 0;
    assert(ssd1677_refresh(&d) == ESP_OK && command_count == 0); /* No-change frame. */
    assert(m->activations == total.activations && total.activations == 10);
    assert(m->mode2_face_visits[0] == 5 && m->mode2_face_visits[1] == 5);
    for (unsigned i = 0; i < 4; ++i) assert(m->classes[i] > 0);
    free(m);
    return total;
}

static void test_failures_and_recovery(void)
{
    for (unsigned failure = 0; failure < 4; ++failure) {
        epd_ssd1677_t d = make_display();
        d.papermono_otp = true; d.band_buffer = band;
        d.fastest_tiles = true; d.dirty_hold_trial = true;
        assert(epd_ssd1677_set_controller_mode(&d, "refresh=fastest") == ESP_OK);
        assert(ssd1677_refresh(&d) == ESP_OK);
        flip(0, 0); assert(ssd1677_refresh(&d) == ESP_OK); /* Full transition + sync. */
        assert(d.dirty_hold_ram_synced);
        const uint8_t previous = shadow[0];
        flip(0, 0); flip(0, 16);
        command_count = 0;
        if (failure == 0) { fail_plane_command = 0x24; fail_plane_after_bytes = 1; }
        if (failure == 1) fail_command = 0x20;
        if (failure == 2) { fail_plane_command = 0x26; fail_plane_after_bytes = 0; }
        if (failure == 3) busy_on_activation = true;
        assert(ssd1677_refresh(&d) != ESP_OK);
        assert(d.faulted && !d.shadow_valid && !d.dirty_hold_ram_synced);
        assert(shadow[0] == previous); /* Never promote on partial failure. */
        const size_t count = command_count;
        assert(ssd1677_refresh(&d) == ESP_ERR_INVALID_STATE && command_count == count);
        stuck_busy = false;
        d = make_display(); /* Existing recovery is reboot + OTP full, not an unsafe retry. */
        d.papermono_otp = true; d.band_buffer = band;
        d.fastest_tiles = true; d.dirty_hold_trial = true;
        assert(ssd1677_refresh(&d) == ESP_OK && d.shadow_valid);
        assert(find_command(0x24, 1)->length == SSD1677_BUFFER_BYTES);
    }
}

static void test_unknown_ram_fallback(void)
{
    epd_ssd1677_t d = make_display();
    d.papermono_otp = true; d.band_buffer = band;
    d.fastest_tiles = true; d.dirty_hold_trial = true;
    assert(epd_ssd1677_set_controller_mode(&d, "refresh=fastest") == ESP_OK);
    assert(ssd1677_refresh(&d) == ESP_OK);
    flip(0, 0); assert(ssd1677_refresh(&d) == ESP_OK);
    assert(d.dirty_hold_ram_synced);
    controller_ram_t *m = malloc(sizeof(*m));
    assert(m);
    controller_seed(m, &d);
    d.reference_awake = false; /* Controller reset / lost RAM retention. */
    flip(1, 0); command_count = 0;
    assert(ssd1677_refresh(&d) == ESP_OK);
    assert(controller_apply(m, &d).activations == 1);
    assert(find_command(0x24, 0)->length == SSD1677_BUFFER_BYTES);
    assert(find_command(0x26, 0)->length == SSD1677_BUFFER_BYTES);
    assert(find_command(0x26, 1)->length == SSD1677_BUFFER_BYTES);
    d.shadow_valid = false; /* Explicitly unknown shadow requires OTP full. */
    flip(2, 0); command_count = 0;
    assert(ssd1677_refresh(&d) == ESP_OK && !d.dirty_hold_ram_synced);
    assert(find_command(0x24, 0)->length == SSD1677_BUFFER_BYTES);
    free(m);
}

static void test_mode_and_sleep_fallback(void)
{
    epd_ssd1677_t d = make_display();
    d.papermono_otp = true; d.band_buffer = band;
    d.fastest_tiles = true; d.dirty_hold_trial = true;
    assert(epd_ssd1677_set_controller_mode(&d, "refresh=fastest") == ESP_OK);
    assert(ssd1677_refresh(&d) == ESP_OK);
    flip(0, 0); assert(ssd1677_refresh(&d) == ESP_OK && d.dirty_hold_ram_synced);
    assert(epd_ssd1677_set_controller_mode(&d, "refresh=typing") == ESP_OK);
    flip(1, 0); command_count = 0;
    assert(ssd1677_refresh(&d) == ESP_OK && !d.dirty_hold_ram_synced);
    assert(find_command(0x24, 0)->length == SSD1677_BUFFER_BYTES);
    assert(find_command(0x26, 0)->length == SSD1677_BUFFER_BYTES);
    assert(epd_ssd1677_set_controller_mode(&d, "refresh=fastest") == ESP_OK);
    flip(2, 0); command_count = 0;
    assert(ssd1677_refresh(&d) == ESP_OK && d.dirty_hold_ram_synced);
    assert(find_command(0x24, 0)->length == SSD1677_BUFFER_BYTES);
    assert(find_command(0x26, 1)->length == SSD1677_BUFFER_BYTES);
    assert(ssd1677_sleep(&d) == ESP_OK && !d.dirty_hold_ram_synced);
    assert(epd_ssd1677_resume(&d) == ESP_OK && !d.dirty_hold_ram_synced);
    flip(3, 0); command_count = 0;
    assert(ssd1677_refresh(&d) == ESP_OK && !d.dirty_hold_ram_synced);
    assert(find_command(0x24, 0)->length == SSD1677_BUFFER_BYTES);
}

static void test_runtime_switch_fallback(void)
{
    epd_ssd1677_t d = make_display();
    d.papermono_otp = true; d.band_buffer = band; d.fastest_tiles = true;
    assert(!d.dirty_hold_trial);
    assert(epd_ssd1677_set_controller_mode(&d, "refresh=fastest") == ESP_OK);
    assert(ssd1677_refresh(&d) == ESP_OK); /* OTP baseline. */
    flip(0, 0); command_count = 0;
    assert(ssd1677_refresh(&d) == ESP_OK);
    assert(find_command(0x24, 0)->length == SSD1677_BUFFER_BYTES);
    assert(find_command(0x26, 0)->length == SSD1677_BUFFER_BYTES);
    assert(epd_ssd1677_set_controller_mode(&d, "dirty-window=columns") == ESP_OK);
    assert(d.dirty_hold_trial && d.dirty_hold_full_height && !d.dirty_hold_ram_synced);
    flip(1, 0); command_count = 0;
    assert(ssd1677_refresh(&d) == ESP_OK && d.dirty_hold_ram_synced);
    assert(find_command(0x24, 0)->length == SSD1677_BUFFER_BYTES);
    assert(find_command(0x26, 0)->length == SSD1677_BUFFER_BYTES);
    assert(find_command(0x26, 1)->length == SSD1677_BUFFER_BYTES);
    flip(2, 0); command_count = 0;
    assert(ssd1677_refresh(&d) == ESP_OK && d.dirty_hold_ram_synced);
    assert(find_command(0x24, 0)->length == SSD1677_HEIGHT);
    assert(find_command(0x26, 0)->length == SSD1677_HEIGHT);
    assert(epd_ssd1677_set_controller_mode(&d, "dirty-window=off") == ESP_OK);
    assert(!d.dirty_hold_trial && !d.dirty_hold_ram_synced);
    flip(3, 0); command_count = 0;
    assert(ssd1677_refresh(&d) == ESP_OK);
    assert(find_command(0x24, 0)->length == SSD1677_BUFFER_BYTES);
    assert(find_command(0x26, 0)->length == SSD1677_BUFFER_BYTES);
    assert(epd_ssd1677_set_controller_mode(&d, "dirty-window=unknown") == ESP_ERR_INVALID_ARG);
    assert(epd_ssd1677_set_controller_mode(&d, "dirty-window=on") == ESP_ERR_INVALID_ARG);
}

static void test_full_height_column_window(void)
{
    epd_ssd1677_t d = make_display();
    d.papermono_otp = true; d.band_buffer = band; d.fastest_tiles = true;
    assert(epd_ssd1677_set_controller_mode(&d, "refresh=fastest") == ESP_OK);
    assert(ssd1677_refresh(&d) == ESP_OK);
    controller_ram_t *m = malloc(sizeof(*m));
    assert(m);
    controller_seed(m, &d);
    assert(epd_ssd1677_set_controller_mode(&d, "dirty-window=columns") == ESP_OK);
    assert(d.dirty_hold_trial && d.dirty_hold_full_height);
    flip(770, 400);
    run_step(&d, m, true, SSD1677_BUFFER_BYTES, true);
    flip(771, 401);
    run_step(&d, m, true, SSD1677_HEIGHT, false);
    const captured_command_t *xb = find_command(0x44, 0);
    const captured_command_t *yb = find_command(0x45, 0);
    assert(xb && xb->length == 4 && xb->data[0] == 0 && xb->data[1] == 3 &&
           xb->data[2] == 7 && xb->data[3] == 3);
    assert(yb && yb->length == 4 && yb->data[0] == 0xdf && yb->data[1] == 1 &&
           yb->data[2] == 0 && yb->data[3] == 0);
    assert(epd_ssd1677_set_controller_mode(&d, "dirty-window=off") == ESP_OK);
    assert(!d.dirty_hold_trial && !d.dirty_hold_ram_synced);
    free(m);
}

int main(void)
{
    const double bounds_us = benchmark_bounds_us();
    const transfer_t baseline = run_series(false);
    const transfer_t candidate = run_series(true);
    assert(baseline.activations == candidate.activations);
    assert(candidate.bw_bytes < baseline.bw_bytes);
    assert(candidate.red_bytes < baseline.red_bytes);
    assert(candidate.spi_bytes < baseline.spi_bytes);
    assert(candidate.converted_bytes < baseline.converted_bytes);
    printf("dirty-hold baseline: bw=%zu red=%zu spi=%zu converted=%zu updates=%u host_cpu_ms=%.3f\n",
           baseline.bw_bytes, baseline.red_bytes, baseline.spi_bytes,
           baseline.converted_bytes, baseline.activations, baseline.host_driver_ms);
    printf("dirty-hold candidate: bw=%zu red=%zu spi=%zu converted=%zu updates=%u host_cpu_ms=%.3f\n",
           candidate.bw_bytes, candidate.red_bytes, candidate.spi_bytes,
           candidate.converted_bytes, candidate.activations, candidate.host_driver_ms);
    printf("dirty-hold shared-bounds host_cpu_us=%.3f per call (500 calls)\n", bounds_us);
    test_failures_and_recovery();
    test_unknown_ram_fallback();
    test_mode_and_sleep_fallback();
    test_runtime_switch_fallback();
    test_full_height_column_window();
    puts("epd_ssd1677_dirty_hold_test: PASS");
    return 0;
}
