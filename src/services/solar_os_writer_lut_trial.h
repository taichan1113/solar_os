#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifndef SOLAR_OS_SSD1677_WRITER_D_TRIAL
#define SOLAR_OS_SSD1677_WRITER_D_TRIAL 0
#endif

#if SOLAR_OS_SSD1677_WRITER_D_TRIAL
/* Explicit origin, copied WITH each immutable snapshot. No foreground lookup.
 * Times begin at Writer event acceptance, not BLE reception; no key text. */
typedef struct {
    bool is_writer;
    uint32_t inputs, coalesced;
    uint64_t generation;
    int64_t input_first_us, input_last_us, render_start_us, rendered_us;
    int64_t queued_first_us, queued_us;
} solar_os_writer_frame_t;
#endif
