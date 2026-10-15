#pragma once

#include "solar_os_buttons.h"
#include "solar_os_keys.h"

#define SOLAR_OS_BOARD_HAS_BUTTONS 1
#define SOLAR_OS_BOARD_BUTTONS { \
    {.pin = 3, .name = "KEY2", .key = SOLAR_OS_KEY_ENTER, \
     .active_low = true, .emit_on_release = true, \
     .long_press_key = SOLAR_OS_KEY_ESCAPE, .long_press_ms = 1200U, \
     .pull = SOLAR_OS_BUTTON_PULL_UP} \
}
