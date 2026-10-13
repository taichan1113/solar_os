#include <assert.h>
#include <stdio.h>
#include "solar_os_sketch_trace_core.h"

int main(void)
{
    solar_os_sketch_trace_core_t core = {0};
    solar_os_sketch_trace_row_t rows[3] = {0};
    solar_os_sketch_trace_core_start(&core, rows, 3);
    assert(core.enabled && core.count == 0);
    const uint32_t press = solar_os_sketch_trace_core_point(&core, true);
    const uint32_t move = solar_os_sketch_trace_core_point(&core, false);
    const uint32_t release = solar_os_sketch_trace_core_point(&core, false);
    assert(press == 1 && move == 2 && release == 3 && core.stroke == 1);
    assert(solar_os_sketch_trace_core_append(&core,
        (solar_os_sketch_trace_row_t){.time_us=100,.point=press,.kind=SKTRACE_SAMPLE}));
    assert(solar_os_sketch_trace_core_append(&core,
        (solar_os_sketch_trace_row_t){.time_us=110,.point=move,.kind=SKTRACE_CANVAS,.version=2}));
    assert(solar_os_sketch_trace_core_append(&core,
        (solar_os_sketch_trace_row_t){.time_us=120,.point=release,.kind=SKTRACE_SUBMIT,.generation=7}));
    assert(!solar_os_sketch_trace_core_append(&core,
        (solar_os_sketch_trace_row_t){.time_us=130,.kind=SKTRACE_WORKER_END}));
    assert(core.count == 3 && core.overflow == 1 && rows[2].generation == 7);
    solar_os_sketch_trace_core_stop(&core);
    assert(!solar_os_sketch_trace_core_append(&core,
        (solar_os_sketch_trace_row_t){.time_us=140}));
    assert(core.overflow == 1 && !core.enabled);
    solar_os_sketch_trace_core_start(&core, rows, 3);
    assert(core.count == 0 && core.overflow == 0 && core.point == 0);
    puts("sketch trace core: bounded IDs and overflow PASS");
    return 0;
}
