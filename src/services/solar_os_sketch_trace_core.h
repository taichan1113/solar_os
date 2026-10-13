#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Plain C journal logic, shared by the trial firmware and Host tests. */
typedef enum {
    SKTRACE_READ = 1, SKTRACE_SAMPLE, SKTRACE_PUSH, SKTRACE_POP,
    SKTRACE_DISPATCH, SKTRACE_EVENT, SKTRACE_CANVAS, SKTRACE_RENDER,
    SKTRACE_XBM, SKTRACE_BITMAP, SKTRACE_UI, SKTRACE_SUBMIT,
    SKTRACE_REPLACE, SKTRACE_WORKER_START, SKTRACE_WORKER_END,
    SKTRACE_SSD, SKTRACE_SUSPEND, SKTRACE_RESUME,
    SKTRACE_SSD_CONVERT, SKTRACE_SSD_SPI, SKTRACE_SSD_BUSY,
} solar_os_sketch_trace_kind_t;

typedef struct {
    int64_t time_us;
    uint64_t generation;
    uint32_t point;
    uint32_t version;
    int32_t a, b;
    uint16_t kind;
} solar_os_sketch_trace_row_t;

typedef struct {
    solar_os_sketch_trace_row_t *rows;
    size_t capacity, count;
    uint32_t overflow, stroke, point;
    bool enabled;
} solar_os_sketch_trace_core_t;

void solar_os_sketch_trace_core_start(solar_os_sketch_trace_core_t *core,
    solar_os_sketch_trace_row_t *rows, size_t capacity);
void solar_os_sketch_trace_core_stop(solar_os_sketch_trace_core_t *core);
uint32_t solar_os_sketch_trace_core_point(solar_os_sketch_trace_core_t *core,
    bool press);
bool solar_os_sketch_trace_core_append(solar_os_sketch_trace_core_t *core,
    solar_os_sketch_trace_row_t row);
