#include "solar_os_sketch_trace_core.h"

#include <string.h>

void solar_os_sketch_trace_core_start(solar_os_sketch_trace_core_t *core,
    solar_os_sketch_trace_row_t *rows, size_t capacity)
{
    if (!core) return;
    memset(core, 0, sizeof(*core));
    core->rows = rows;
    core->capacity = capacity;
    core->enabled = rows != NULL && capacity > 0;
}

void solar_os_sketch_trace_core_stop(solar_os_sketch_trace_core_t *core)
{
    if (core) core->enabled = false;
}

uint32_t solar_os_sketch_trace_core_point(solar_os_sketch_trace_core_t *core,
    bool press)
{
    if (!core || !core->enabled) return 0;
    if (press) core->stroke++;
    if (core->point != UINT32_MAX) core->point++;
    return core->point;
}

bool solar_os_sketch_trace_core_append(solar_os_sketch_trace_core_t *core,
    solar_os_sketch_trace_row_t row)
{
    if (!core || !core->enabled) return false;
    if (core->count >= core->capacity) {
        if (core->overflow != UINT32_MAX) core->overflow++;
        return false;
    }
    core->rows[core->count++] = row;
    return true;
}
