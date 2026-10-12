/* Exercise production Files rendering and paging against a bounded cell grid. */
#include <assert.h>
#include "freertos/queue.h"
BaseType_t xQueueOverwrite(QueueHandle_t queue, const void *item);
#include "../../src/apps/solar_os_files.c"

static size_t rows, cols;
static char cells[40][121];
static uint8_t attrs[40][120];
static bool too_small;
static solar_os_tui_rect_t transaction_bounds;

size_t strlcpy(char *dst, const char *src, size_t size)
{
    size_t len = strlen(src);
    if (size) { size_t n = len < size - 1 ? len : size - 1; memcpy(dst, src, n); dst[n] = 0; }
    return len;
}
size_t solar_os_tui_rows(const solar_os_tui_t *tui) { (void)tui; return rows; }
size_t solar_os_tui_cols(const solar_os_tui_t *tui) { (void)tui; return cols; }
bool solar_os_tui_screen_fullscreen(const solar_os_tui_t *tui) { return tui->fullscreen; }
void solar_os_tui_clear(solar_os_tui_t *tui)
{
    (void)tui;
    memset(cells, ' ', sizeof(cells)); memset(attrs, 0, sizeof(attrs));
    for (size_t r = 0; r < 40; r++) cells[r][cols] = 0;
    too_small = false;
}
void solar_os_tui_refresh(solar_os_tui_t *tui) { (void)tui; }
esp_err_t solar_os_tui_set_cursor_visible(solar_os_tui_t *tui, bool visible)
{ tui->cursor_visible = visible; return ESP_OK; }
esp_err_t solar_os_tui_move(solar_os_tui_t *tui, size_t row, size_t col)
{ assert(row < rows && col < cols); tui->cursor_row = row; tui->cursor_col = col; return ESP_OK; }
esp_err_t solar_os_tui_putch(solar_os_tui_t *tui, size_t row, size_t col, uint32_t ch, uint8_t attr)
{ (void)tui; assert(row < rows && col < cols); cells[row][col] = (char)ch; attrs[row][col] = attr; return ESP_OK; }
esp_err_t solar_os_tui_fill(solar_os_tui_t *tui, size_t row, size_t col, size_t height,
                           size_t width, uint32_t ch, uint8_t attr)
{
    assert(row + height <= rows && col + width <= cols);
    for (size_t r = row; r < row + height; r++)
        for (size_t c = col; c < col + width; c++) solar_os_tui_putch(tui, r, c, ch, attr);
    return ESP_OK;
}
esp_err_t solar_os_tui_write_cell(solar_os_tui_t *tui, size_t row, size_t col,
                                 size_t width, const char *text, uint8_t attr)
{
    solar_os_tui_fill(tui, row, col, 1, width, ' ', attr);
    for (size_t c = 0; c < width && text[c]; c++) solar_os_tui_putch(tui, row, col + c, text[c], attr);
    return ESP_OK;
}
esp_err_t solar_os_tui_box(solar_os_tui_t *tui, size_t row, size_t col,
                          size_t height, size_t width, uint8_t attr)
{
    solar_os_tui_fill(tui, row, col, 1, width, '-', attr);
    solar_os_tui_fill(tui, row + height - 1, col, 1, width, '-', attr);
    solar_os_tui_fill(tui, row, col, height, 1, '|', attr);
    solar_os_tui_fill(tui, row, col + width - 1, height, 1, '|', attr);
    return ESP_OK;
}
esp_err_t solar_os_tui_draw_too_small(solar_os_tui_t *tui, const char *name)
{ (void)tui; assert(!strcmp(name, "files")); too_small = true; return ESP_OK; }
esp_err_t solar_os_tui_draw_footer(solar_os_tui_t *tui, const char *status, const char *help)
{
    (void)help; assert(tui->fullscreen);
    return status[0] ? solar_os_tui_write_cell(tui, rows - 1, 0, cols, status, SOLAR_OS_TUI_ATTR_INVERSE) : ESP_OK;
}
esp_err_t solar_os_tui_text_popup(solar_os_tui_t *tui, const solar_os_tui_rect_t *bounds,
                                 const char *title, const char *text, solar_os_tui_rect_t *popup)
{
    (void)tui; (void)title; (void)text;
    assert(bounds->row + bounds->height <= rows && bounds->col + bounds->width <= cols);
    transaction_bounds = *bounds; *popup = *bounds; return ESP_OK;
}
esp_err_t solar_os_tui_progress_bar(solar_os_tui_t *tui, size_t row, size_t col, size_t width,
                                   const char *label, uint64_t value, uint64_t total, bool known)
{ (void)label; (void)value; (void)total; (void)known; return solar_os_tui_fill(tui, row, col, 1, width, '=', 0); }

static files_state_t state;
static files_entry_t entries[65];
static void setup(size_t r, size_t c, bool fullscreen, bool launcher, files_input_mode_t mode)
{
    rows = r; cols = c; memset(&state, 0, sizeof(state)); files_state = &state;
    files.tui.fullscreen = fullscreen; files.launcher_mode = launcher; files.input_mode = mode;
    strlcpy(files.message, "test message", sizeof(files.message));
    strlcpy(files.input, "example", sizeof(files.input)); files.input_len = strlen(files.input);
    strlcpy(files.search.input, "example", sizeof(files.search.input)); files.search.input_len = 7;
    for (size_t p = 0; p < 2; p++) {
        files.panes[p].entries = entries; files.panes[p].count = 65;
        strlcpy(files.panes[p].path, p ? "/right" : "/left", sizeof(files.panes[p].path));
    }
}
static void legend(void)
{
    static const char *expected[] = {"F3 View", "F4 Edit", "F5 Copy", "F6 Move",
                                    "F7 mKdir", "F8 Delete", "F9 Zip", "Alt+S Search"};
    static const size_t bold[] = {3, 3, 3, 3, 4, 3, 3, 6};
    files_layout_t l = files_layout(rows, cols);
    size_t item = 0;
    for (size_t r = rows - l.help_rows; r < rows; r++) {
        size_t c = 0;
        while (item < 8 && (r == rows - 1 || item < l.help_split)) {
            size_t n = strlen(expected[item]);
            assert(!memcmp(&cells[r][c], expected[item], n));
            for (size_t j = 0; j < n; j++)
                assert(attrs[r][c + j] == (SOLAR_OS_TUI_ATTR_INVERSE |
                       (j == bold[item] ? SOLAR_OS_TUI_ATTR_BOLD : 0)));
            c += n; item++;
            if (item < 8 && (r == rows - 1 || item < l.help_split)) assert(cells[r][c++] == ' ');
        }
        for (; c < cols; c++) assert(cells[r][c] == ' ' && attrs[r][c] == SOLAR_OS_TUI_ATTR_INVERSE);
    }
    assert(item == 8);
}
static void paging(void)
{
    files_layout_t l = files_layout(rows, cols);
    for (files.active = 0; files.active < (files.launcher_mode ? 1 : 2); files.active++) {
        files_pane_t *p = &files.panes[files.active];
        for (size_t i = 0; i < 70; i++) {
            size_t before = p->cursor;
            files_page(p, true); files_render(NULL);
            size_t expected = before + l.list_rows < 65 ? before + l.list_rows : 64;
            assert(p->cursor == expected && p->cursor >= p->top && p->cursor < p->top + l.list_rows);
            size_t col = files.active ? cols / 2 + 1 : 1;
            assert(attrs[2 + p->cursor - p->top][col] & SOLAR_OS_TUI_ATTR_INVERSE);
        }
        assert(p->cursor == 64);
        for (size_t i = 0; i < 70; i++) {
            size_t before = p->cursor; files_page(p, false); files_render(NULL);
            assert(p->cursor == (before > l.list_rows ? before - l.list_rows : 0));
            assert(p->cursor >= p->top && p->cursor < p->top + l.list_rows);
        }
        assert(p->cursor == 0);
    }
    files.active = 0;
}
int main(void)
{
    for (size_t i = 0; i < 65; i++) snprintf(entries[i].name, sizeof(entries[i].name), "file%02zu", i);
    setup(23, 80, false, false, FILES_INPUT_NONE);
    files_render(NULL); legend();
    assert(files_layout(rows, cols).help_rows == 1 && files_layout(rows, cols).list_rows == 18);
    assert(!strncmp(cells[22], "F3 View F4 Edit F5 Copy F6 Move F7 mKdir F8 Delete F9 Zip Alt+S Search", 70));
    paging();
    setup(15, 56, false, false, FILES_INPUT_NONE);
    files_render(NULL); legend();
    assert(files_layout(rows, cols).help_rows == 2 && files_layout(rows, cols).list_rows == 9);
    assert(!strncmp(cells[13], "F3 View F4 Edit F5 Copy F6 Move", 31));
    assert(!strncmp(cells[14], "F7 mKdir F8 Delete F9 Zip Alt+S Search", 38));
    paging();
    setup(15, 69, false, false, FILES_INPUT_NONE);
    assert(files_layout(rows, cols).help_rows == 2);
    setup(15, 70, false, false, FILES_INPUT_NONE);
    assert(files_layout(rows, cols).help_rows == 1);
    setup(15, 37, false, false, FILES_INPUT_NONE);
    assert(!files_layout(rows, cols).valid);
    setup(15, 38, false, false, FILES_INPUT_NONE);
    assert(files_layout(rows, cols).valid);
    /* Every width/height, input mode, fullscreen and launcher combination. */
    for (size_t c = 0; c <= 120; c++) for (size_t r = 0; r <= 30; r++)
        for (unsigned flags = 0; flags < 4; flags++) for (unsigned mode = 0; mode <= FILES_INPUT_SEARCH; mode++) {
            setup(r, c, flags & 1, flags & 2, mode);
            files_layout_t l = files_layout(rows, cols);
            files_render(NULL);
            assert(too_small == !l.valid);
            if (!l.valid) { files_page(&files.panes[0], true); assert(files.panes[0].cursor == 0); continue; }
            assert(l.list_rows + 3 + l.footer_rows == rows);
            if (files.launcher_mode) assert(l.help_rows == 0 && l.footer_rows == 0);
            else if (files.tui.fullscreen) assert(l.help_rows == 0 && l.footer_rows == (mode ? 1U : 0U));
            else { legend(); assert(l.help_rows == (c >= 70 ? 1U : 2U)); }
            if (!files.launcher_mode) {
                size_t msg_row = files.tui.fullscreen && mode == FILES_INPUT_NONE ? rows - 1 : rows - l.footer_rows;
                const char *message = mode == FILES_INPUT_MKDIR ? "mkdir: example" :
                    mode == FILES_INPUT_ZIP ? "zip: example" :
                    mode == FILES_INPUT_SEARCH ? "search: example" : "test message";
                assert(!strncmp(cells[msg_row], message, strlen(message)));
                if (mode == FILES_INPUT_DELETE_CONFIRM)
                    assert(attrs[msg_row][0] == SOLAR_OS_TUI_ATTR_BOLD);
            }
            if (!files.launcher_mode && (mode == FILES_INPUT_MKDIR || mode == FILES_INPUT_ZIP || mode == FILES_INPUT_SEARCH)) {
                assert(files.tui.cursor_row == rows - l.footer_rows);
                /* Long input stays above the legend and the cursor stays in bounds. */
                memset(files.input, 'x', sizeof(files.input) - 1); files.input_len = sizeof(files.input) - 1;
                memset(files.search.input, 'x', sizeof(files.search.input) - 1); files.search.input_len = sizeof(files.search.input) - 1;
                files_render(NULL); assert(files.tui.cursor_col < cols);
            }
            if (!files.launcher_mode) {
                files.transaction.active = true; files.transaction.kind = FILES_TRANSACTION_COPY;
                files_render(NULL); assert(transaction_bounds.row == 1 && transaction_bounds.height == l.pane_height);
                assert(transaction_bounds.row + transaction_bounds.height <= rows - l.footer_rows);
            }
        }
    for (unsigned flags = 1; flags <= 2; flags++) for (unsigned mode = 0; mode <= FILES_INPUT_SEARCH; mode++) {
        setup(15, 56, flags == 1, flags == 2, mode); paging();
    }
    puts("files_layout_test: PASS (bounded render, legend attributes, inputs, popups and paging)");
    return 0;
}
