#pragma once

#include "solar_os_board.h"
#include "solar_os_gfx.h"
#include "solar_os_terminal.h"

/* Roles select existing bitmap faces; explicit GFX sizes and document zoom
 * are independent. Normal reproduces the existing terminal/UI geometry. */
#define SOLAR_OS_UI_FONT_PRESET_NORMAL 0
#define SOLAR_OS_UI_FONT_PRESET_READABLE 1
#define SOLAR_OS_UI_FONT_PRESET_LARGE 2
#ifndef SOLAR_OS_BOARD_UI_FONT_PRESET
#define SOLAR_OS_BOARD_UI_FONT_PRESET SOLAR_OS_UI_FONT_PRESET_NORMAL
#endif
#if SOLAR_OS_BOARD_UI_FONT_PRESET < 0 || SOLAR_OS_BOARD_UI_FONT_PRESET > 2
#error "UI font preset must be normal (0), readable (1), or large (2)"
#endif

/* Slow panels can retain status changes until another action requests a draw.
 * This policy changes neither the status data nor the display transport. */
#ifndef SOLAR_OS_BOARD_TERMINAL_STATUS_REFRESH_ON_CHANGE
#define SOLAR_OS_BOARD_TERMINAL_STATUS_REFRESH_ON_CHANGE 1
#endif

typedef struct {
    const char *name;
    solar_os_terminal_font_t terminal_font;
    solar_os_terminal_text_size_t terminal_size;
    solar_os_gfx_font_t small;
    solar_os_gfx_font_t body;
    solar_os_gfx_font_t bold;
    solar_os_gfx_font_t title;
    solar_os_gfx_font_t heading;
    solar_os_gfx_font_t dialog_title;
    uint8_t small_px;
    uint8_t body_px;
    uint8_t title_px;
    uint8_t heading_px;
    uint8_t writer_header_height;
    uint8_t writer_title_baseline;
    uint8_t status_height;
} solar_os_ui_font_profile_t;

static inline const solar_os_ui_font_profile_t *solar_os_ui_font_profile_for(unsigned preset)
{
    static const solar_os_ui_font_profile_t profiles[] = {
        {.name = "normal", .terminal_font = SOLAR_OS_TERMINAL_FONT_COMPACT,
         .terminal_size = SOLAR_OS_TERMINAL_TEXT_SIZE_16,
         .small = SOLAR_OS_GFX_FONT_SMALL, .body = SOLAR_OS_GFX_FONT_MONO_14,
         .bold = SOLAR_OS_GFX_FONT_BOLD_14, .title = SOLAR_OS_GFX_FONT_SMALL,
         .heading = SOLAR_OS_GFX_FONT_BOLD_14, .dialog_title = SOLAR_OS_GFX_FONT_MONO_14,
         .small_px = 12, .body_px = 14, .title_px = 12,
         .heading_px = 14,
         .writer_header_height = 16, .writer_title_baseline = 11, .status_height = 16},
        {.name = "readable", .terminal_font = SOLAR_OS_TERMINAL_FONT_MONO,
         .terminal_size = SOLAR_OS_TERMINAL_TEXT_SIZE_16,
         .small = SOLAR_OS_GFX_FONT_MONO_14, .body = SOLAR_OS_GFX_FONT_MONO_14,
         .bold = SOLAR_OS_GFX_FONT_BOLD_14, .title = SOLAR_OS_GFX_FONT_MONO_16,
         .heading = SOLAR_OS_GFX_FONT_BOLD_16, .dialog_title = SOLAR_OS_GFX_FONT_MONO_16,
         .small_px = 14, .body_px = 14, .title_px = 16,
         .heading_px = 16,
         .writer_header_height = 20, .writer_title_baseline = 15, .status_height = 20},
        {.name = "large", .terminal_font = SOLAR_OS_TERMINAL_FONT_MONO,
         .terminal_size = SOLAR_OS_TERMINAL_TEXT_SIZE_18,
         .small = SOLAR_OS_GFX_FONT_MONO_16, .body = SOLAR_OS_GFX_FONT_MONO_16,
         .bold = SOLAR_OS_GFX_FONT_BOLD_16, .title = SOLAR_OS_GFX_FONT_MONO_18,
         .heading = SOLAR_OS_GFX_FONT_BOLD_18, .dialog_title = SOLAR_OS_GFX_FONT_MONO_18,
         .small_px = 16, .body_px = 16, .title_px = 18,
         .heading_px = 18,
         .writer_header_height = 24, .writer_title_baseline = 17, .status_height = 24},
    };
    return &profiles[preset <= SOLAR_OS_UI_FONT_PRESET_LARGE ? preset : 0];
}

static inline const solar_os_ui_font_profile_t *solar_os_ui_font_profile(void)
{
    return solar_os_ui_font_profile_for(SOLAR_OS_BOARD_UI_FONT_PRESET);
}
