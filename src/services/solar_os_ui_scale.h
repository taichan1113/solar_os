#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "solar_os_board.h"

#ifndef SOLAR_OS_BOARD_UI_SCALE_PERCENT
#define SOLAR_OS_BOARD_UI_SCALE_PERCENT 100
#endif
#if SOLAR_OS_BOARD_UI_SCALE_PERCENT != 100 && SOLAR_OS_BOARD_UI_SCALE_PERCENT != 140
#error "UI scale must be 100 or 140"
#endif

/* Logical UI dimensions; document/image/canvas coordinates stay physical. */
#if SOLAR_OS_BOARD_UI_SCALE_PERCENT == 140
unsigned solar_os_ui_scale_percent(void);
bool solar_os_ui_scale_set_percent(unsigned percent);
#else
static inline unsigned solar_os_ui_scale_percent(void) { return 100U; }
static inline bool solar_os_ui_scale_set_percent(unsigned percent) { return percent == 100U; }
#endif
static inline int solar_os_ui_px(int pixels)
{
    const int value = pixels * (int)solar_os_ui_scale_percent();
    return (value + (value < 0 ? -50 : 50)) / 100;
}

