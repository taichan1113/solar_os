#pragma once

#include "solar_os_sketch_trace_core.h"
#include "esp_err.h"

/* Available only in SOLAR_OS_SKETCH_LATENCY_TRIAL firmware. */
esp_err_t solar_os_sketch_trace_start(void);
void solar_os_sketch_trace_stop(void);
bool solar_os_sketch_trace_active(void);
uint32_t solar_os_sketch_trace_new_point(bool press, uint32_t *stroke);
void solar_os_sketch_trace_record(solar_os_sketch_trace_kind_t kind,
    uint32_t point, uint32_t version, uint64_t generation, int32_t a, int32_t b);
void solar_os_sketch_trace_set_current(uint32_t point);
uint32_t solar_os_sketch_trace_current(void);
size_t solar_os_sketch_trace_count(void);
uint32_t solar_os_sketch_trace_overflow(void);
bool solar_os_sketch_trace_row(size_t index, solar_os_sketch_trace_row_t *row);
uint64_t solar_os_sketch_trace_observer_us(void);
uint32_t solar_os_sketch_trace_observer_max_us(void);
