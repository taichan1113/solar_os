#pragma once

#include "esp_timer.h"
#include "solar_os_log.h"

/* Opt-in pipeline diagnostics follow the existing debug log setting. Check at
 * operation boundaries, never inside pixel/row loops. All times are monotonic
 * microseconds, including the SSD1677 timing=on diagnostics. */
static inline bool solar_os_perf_enabled(void)
{
    solar_os_log_status_t status;
    return solar_os_log_get_status(&status) == ESP_OK &&
           status.level == SOLAR_OS_LOG_LEVEL_DEBUG;
}
