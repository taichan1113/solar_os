#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nvs.h"
#include "solar_os_display.h"
#include "solar_os_log.h"
#include "solar_os_memory.h"
#include "solar_os_terminal_internal.h"
#include "solar_os_ui_font.h"
#if SOLAR_OS_BOARD_UI_SCALE_PERCENT == 140
#include "solar_os_gfx_internal.h"
#include "solar_os_font_face.h"
#endif

static unsigned presents;
static uint8_t frame[800 * 480 / 8];
static const u8x8_display_info_t display_info = {
    .tile_width = 100, .tile_height = 60, .pixel_width = 800, .pixel_height = 480,
};
static uint8_t display_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg, void *ptr)
{
    (void)arg; (void)ptr;
    if (msg == U8X8_MSG_DISPLAY_SETUP_MEMORY) {
        u8x8_d_helper_display_setup_memory(u8x8, &display_info);
    }
    return 1;
}
static uint8_t empty_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg, void *ptr)
{
    (void)u8x8; (void)msg; (void)arg; (void)ptr;
    return 1;
}
esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *handle)
{
    (void)name; (void)mode; (void)handle;
#if SOLAR_OS_BOARD_UI_SCALE_PERCENT == 140
    *handle = 1;
    return ESP_OK;
#else
    return ESP_ERR_NVS_NOT_FOUND;
#endif
}
esp_err_t nvs_get_u16(nvs_handle_t handle, const char *key, uint16_t *value)
{
    (void)handle; (void)key; (void)value;
#if SOLAR_OS_BOARD_UI_SCALE_PERCENT == 140
    if (strcmp(key, "font") == 0) { *value = SOLAR_OS_TERMINAL_FONT_MONO; return ESP_OK; }
    if (strcmp(key, "textsize") == 0) { *value = SOLAR_OS_TERMINAL_TEXT_SIZE_20; return ESP_OK; }
#endif
    return ESP_ERR_NVS_NOT_FOUND;
}
void nvs_close(nvs_handle_t handle) { (void)handle; }
void *solar_os_memory_calloc(size_t count, size_t size,
                            solar_os_memory_class_t cls, const char *tag)
{
    (void)cls; (void)tag;
    return calloc(count, size);
}
void solar_os_memory_free(void *ptr) { free(ptr); }
esp_err_t solar_os_log_write(solar_os_log_level_t level, const char *tag,
                            const char *format, ...)
{
    (void)level; (void)tag; (void)format;
    return ESP_OK;
}
esp_err_t solar_os_display_request_present_mode(u8g2_t *u, solar_os_display_present_mode_t mode)
{
    (void)u; (void)mode;
    return ESP_OK;
}
void solar_os_display_present(u8g2_t *u, solar_os_display_present_mode_t mode)
{
    (void)u; (void)mode;
    presents++;
}
size_t strlcpy(char *dst, const char *src, size_t size)
{
    size_t length = strlen(src);
    if (size) {
        size_t copy = length < size - 1 ? length : size - 1;
        memcpy(dst, src, copy); dst[copy] = 0;
    }
    return length;
}
static uint32_t hash_frame(void)
{
    uint32_t hash = 2166136261U;
    for (size_t i = 0; i < sizeof(frame); i++) hash = (hash ^ frame[i]) * 16777619U;
    return hash;
}
static void write_frame(const char *path)
{
    FILE *out = fopen(path, "wb");
    assert(out);
    fprintf(out, "P4\n800 480\n");
    for (int y = 0; y < 480; y++) {
        for (int x = 0; x < 800; x += 8) {
            uint8_t pixels = 0;
            for (int bit = 0; bit < 8; bit++) {
                bool black = (frame[(y / 8) * 800 + x + bit] & (1U << (y % 8))) == 0;
                pixels |= black ? 0x80U >> bit : 0;
            }
            fputc(pixels, out);
        }
    }
    fclose(out);
}
int main(int argc, char **argv)
{
    u8g2_t u = {0};
    u8g2_SetupDisplay(&u, display_cb, u8x8_cad_empty, u8x8_byte_empty, empty_cb);
    u8g2_SetupBuffer(&u, frame, 60, u8g2_ll_hvline_vertical_top_lsb, U8G2_R0);
    solar_os_terminal_t *term = calloc(1, sizeof(*term));
    assert(term);
    solar_os_terminal_init_with_rotation(term, &u, U8G2_R0);
    const solar_os_ui_font_profile_t *ui = solar_os_ui_font_profile();
#if SOLAR_OS_BOARD_UI_SCALE_PERCENT == 140
    assert(term->font == SOLAR_OS_TERMINAL_FONT_MONO && term->text_size == SOLAR_OS_TERMINAL_TEXT_SIZE_20);
#else
    assert(term->font == ui->terminal_font && term->text_size == ui->terminal_size);
#endif
    assert(term->status_bar_height == ui->status_height);
    assert(term->baseline_offset >= term->status_bar_height);
    assert(term->baseline_offset + (term->rows - 1) * term->line_height <= 480);
    const char *text = "Welcome to SolarOS\n"
        "status  version  board  commands  apps\n"
        "ls  session list  mem  ble status  power status\n"
        "writer /transport-test.md\n"
        "JIS symbols: @ : ; + ^ ~ [ ] { } \\\n"
        "user@sol:/ ";
    for (const char *p = text; *p; p++) solar_os_terminal_put_char(term, *p);
    solar_os_terminal_draw(term);
    assert(presents == 1 && !solar_os_terminal_needs_draw(term));
    uint32_t initial_hash = hash_frame();
    if (argc > 1) write_frame(argv[1]);

    solar_os_status_bar_t status = {.bluetooth_supported = true, .bluetooth_enabled = true};
    for (unsigned i = 0; i < 120; i++) {
        status.bluetooth_scanning = i % 2;
        status.time_valid = true;
        status.minute = i % 60;
        status.battery_valid = true;
        status.battery_percent = i % 101;
        status.battery_external_power = i % 2;
        solar_os_terminal_set_status_bar(term, &status);
#if SOLAR_OS_BOARD_TERMINAL_STATUS_REFRESH_ON_CHANGE
        assert(solar_os_terminal_needs_draw(term));
        solar_os_terminal_draw(term);
#else
        assert(!solar_os_terminal_needs_draw(term));
        assert(presents == 1 && hash_frame() == initial_hash);
#endif
    }
    solar_os_status_bar_t current;
    solar_os_terminal_get_status_bar(term, &current);
    assert(current.minute == 59 && current.bluetooth_scanning);
    const unsigned before_input = presents;
    solar_os_terminal_put_char(term, 'v');
    assert(solar_os_terminal_needs_draw(term));
    solar_os_terminal_draw(term);
    assert(presents == before_input + 1 && hash_frame() != initial_hash);
    const uint32_t input_hash = hash_frame();
    /* Repainting identical text/cursor/status must not present another frame. */
    solar_os_terminal_draw(term);
    assert(presents == before_input + 1 && hash_frame() == input_hash);
    /* Resume invalidates the retained display and must redraw once. */
    solar_os_terminal_invalidate_render(term);
    solar_os_terminal_draw(term);
    assert(presents == before_input + 2 && hash_frame() == input_hash);
    /* Same text and clock: distinguish battery fill, unavailable slash, plug.
     * An explicit redraw here tests pixels, without changing status policy. */
    uint32_t icon_hash[5];
    const uint8_t percents[] = {0, 50, 100, 0, 100};
    for (unsigned i = 0; i < 5; i++) {
        status.battery_valid = true;
        status.battery_percent = percents[i];
        status.battery_external_power = i >= 3;
        solar_os_terminal_set_status_bar(term, &status);
        solar_os_terminal_invalidate_render(term);
        solar_os_terminal_draw(term);
        icon_hash[i] = hash_frame();
    }
    assert(icon_hash[0] != icon_hash[1] && icon_hash[1] != icon_hash[2]);
    assert(icon_hash[0] != icon_hash[3] && icon_hash[3] == icon_hash[4]);
    status.battery_external_power = false;
    status.battery_valid = false;
    solar_os_terminal_set_status_bar(term, &status);
    solar_os_terminal_invalidate_render(term);
    solar_os_terminal_draw(term);
    assert(hash_frame() != icon_hash[0]);
    printf("%s: cols=%zu rows=%zu cell=%ux%u ascent=%u status=%u initial=%08x input=%08x presents=%u\n",
           ui->name, term->cols, term->rows, term->char_width, term->line_height,
           term->cell_ascent, term->status_bar_height, initial_hash, input_hash, presents);
#if SOLAR_OS_BOARD_UI_SCALE_PERCENT == 140
    assert(solar_os_ui_px(12) == 17 && solar_os_ui_px(18) == 25);
    assert(solar_os_ui_px(20) == 28);
    assert(solar_os_terminal_set_text_size_transient(term, SOLAR_OS_TERMINAL_TEXT_SIZE_20) == ESP_OK);
    const size_t large_cols = term->cols, large_rows = term->rows;
    assert(term->text_size == SOLAR_OS_TERMINAL_TEXT_SIZE_20);
    assert(term->char_width >= 14 && term->line_height >= 28);
    assert(large_cols < 80 && large_rows < 20);
    for (size_t i = 0; i < large_cols * (large_rows + 4); i++) solar_os_terminal_put_char(term, 'x');
    assert(term->cursor_row < term->rows && term->cursor_col <= term->cols);
    assert(term->scrollback_count > 0);
    solar_os_gfx_t gfx;
    solar_os_gfx_init(&gfx, &u);
    solar_os_gfx_set_black_is_one(&gfx, true);
    const char *sample = "Mgyp@[]";
    for (unsigned style = 0; style < 4; style++) {
        solar_os_gfx_font_t font = (solar_os_gfx_font_t)(SOLAR_OS_GFX_FONT_MONO_20 + style * 5);
        solar_os_gfx_set_font(&gfx, font);
        u8g2_SetFont(&u, solar_os_font_face(28, style));
        size_t expected = u8g2_GetUTF8Width(&u, sample);
        assert(solar_os_gfx_text_width(&gfx, sample) == expected);
        solar_os_gfx_font_metrics_t fm = solar_os_gfx_font_metrics(&gfx);
        assert(fm.ascent > 0 && fm.descent >= 0 && fm.ascent + fm.descent == fm.height);
        u8g2_ClearBuffer(&u);
        solar_os_gfx_text(&gfx, 20, 80, sample);
        assert(hash_frame() != 0);
        uint8_t drawn[sizeof(frame)]; memcpy(drawn, frame, sizeof(frame));
        u8g2_ClearBuffer(&u);
        u8g2_SetFont(&u, solar_os_font_face(28, style));
        u8g2_SetFontMode(&u, 1); u8g2_SetFontPosBaseline(&u); u8g2_SetDrawColor(&u, 1);
        u8g2_DrawUTF8(&u, 20, 80, sample);
        assert(memcmp(frame, drawn, sizeof(frame)) == 0);
        solar_os_gfx_set_font_native(&gfx, font);
        assert(solar_os_gfx_text_width(&gfx, sample) < expected);
    }
    /* New native document faces keep exact glyph widths and rasters at both
     * UI scales, independently of the shared chrome's font mapping. */
    for (unsigned scale=100;scale<=140;scale+=40) {
        assert(solar_os_ui_scale_set_percent(scale));
        const unsigned sizes[]={22,25,28};
        for (unsigned style=0;style<4;style++) for (unsigned size=0;size<3;size++) {
            solar_os_gfx_font_t font=(solar_os_gfx_font_t)(SOLAR_OS_GFX_FONT_MONO_22+style*3+size);
            solar_os_gfx_set_font_native(&gfx,font);
            u8g2_SetFont(&u,solar_os_font_face(sizes[size],style));
            assert(solar_os_gfx_text_width(&gfx,sample)==u8g2_GetUTF8Width(&u,sample));
            solar_os_gfx_font_metrics_t fm=solar_os_gfx_font_metrics(&gfx);
            assert(fm.ascent>=19 && fm.descent>=4);
            u8g2_ClearBuffer(&u); solar_os_gfx_text(&gfx,20,80,sample);
            uint8_t drawn[sizeof(frame)]; memcpy(drawn,frame,sizeof(frame));
            u8g2_ClearBuffer(&u); u8g2_SetFont(&u,solar_os_font_face(sizes[size],style));
            u8g2_SetFontMode(&u,1); u8g2_SetFontPosBaseline(&u); u8g2_SetDrawColor(&u,1);
            u8g2_DrawUTF8(&u,20,80,sample);
            assert(memcmp(frame,drawn,sizeof(frame))==0);
        }
    }
    char clipped[32];
    solar_os_gfx_set_font(&gfx, SOLAR_OS_GFX_FONT_MONO_20);
    assert(solar_os_gfx_text_fit(&gfx, "ABCDE", 28, clipped, sizeof(clipped)) == 2);
    assert(strcmp(clipped, "AB") == 0);
    solar_os_gfx_text_fit(&gfx, "A\xc2\xb0" "B", 28, clipped, sizeof(clipped));
    assert(strcmp(clipped, "A\xc2\xb0") == 0);
    solar_os_gfx_text_fit(&gfx, clipped, 14, clipped, sizeof(clipped));
    assert(strcmp(clipped, "A") == 0);
    assert(!solar_os_ui_scale_set_percent(120));
    assert(solar_os_ui_scale_set_percent(100));
    solar_os_terminal_refresh_ui_scale(term);
    assert(term->text_size == SOLAR_OS_TERMINAL_TEXT_SIZE_20);
    assert(term->cols > large_cols && term->rows > large_rows);
    assert(term->cursor_row == 0 && term->cursor_col == 0);
    assert(solar_os_ui_scale_set_percent(140));
    solar_os_terminal_refresh_ui_scale(term);
    assert(term->cols == large_cols && term->rows == large_rows);
    puts("UI scale real-font styles, width/raster, native document size, UTF-8 fit, wrap/scroll, runtime roundtrip: PASS");
#endif
    solar_os_terminal_deinit(term);
    free(term);
    return 0;
}
