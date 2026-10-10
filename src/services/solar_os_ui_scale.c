#include "solar_os_ui_scale.h"

#if SOLAR_OS_BOARD_UI_SCALE_PERCENT == 140
#include <stdatomic.h>
static atomic_uint ui_scale = SOLAR_OS_BOARD_UI_SCALE_PERCENT;
unsigned solar_os_ui_scale_percent(void) { return atomic_load_explicit(&ui_scale, memory_order_relaxed); }
bool solar_os_ui_scale_set_percent(unsigned percent)
{
    if (percent != 100U && percent != 140U) return false;
    atomic_store_explicit(&ui_scale, percent, memory_order_relaxed);
    return true;
}
#endif
