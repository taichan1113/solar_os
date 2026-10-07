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
    return ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_get_u16(nvs_handle_t handle, const char *key, uint16_t *value)
{
    (void)handle; (void)key; (void)value;
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
    assert(term->font == ui->terminal_font && term->text_size == ui->terminal_size);
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
    printf("%s: cols=%zu rows=%zu cell=%ux%u ascent=%u status=%u initial=%08x input=%08x presents=%u\n",
           ui->name, term->cols, term->rows, term->char_width, term->line_height,
           term->cell_ascent, term->status_bar_height, initial_hash, input_hash, presents);
    solar_os_terminal_deinit(term);
    free(term);
    return 0;
}
