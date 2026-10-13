#include "solar_os_sketch_trace.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "solar_os_memory.h"

#define SKETCH_TRACE_CAPACITY 4096U

static portMUX_TYPE trace_lock = portMUX_INITIALIZER_UNLOCKED;
static solar_os_sketch_trace_core_t trace_core;
static solar_os_sketch_trace_row_t *trace_rows;
static bool trace_enabled;
static uint32_t current_point; /* main task only; never used by the touch worker */
static uint64_t observer_us;
static uint32_t observer_max_us;

esp_err_t solar_os_sketch_trace_start(void)
{
    if (!trace_rows) {
        trace_rows = solar_os_memory_calloc(SKETCH_TRACE_CAPACITY,
            sizeof(*trace_rows), SOLAR_OS_MEMORY_EXTERNAL_PREFERRED,
            "sketch-trace");
        if (!trace_rows) return ESP_ERR_NO_MEM;
    }
    portENTER_CRITICAL(&trace_lock);
    __atomic_store_n(&trace_enabled, false, __ATOMIC_RELEASE);
    solar_os_sketch_trace_core_start(&trace_core, trace_rows,
                                     SKETCH_TRACE_CAPACITY);
    current_point = 0;
    observer_us = observer_max_us = 0;
    __atomic_store_n(&trace_enabled, true, __ATOMIC_RELEASE);
    portEXIT_CRITICAL(&trace_lock);
    return ESP_OK;
}

void solar_os_sketch_trace_stop(void)
{
    portENTER_CRITICAL(&trace_lock);
    __atomic_store_n(&trace_enabled, false, __ATOMIC_RELEASE);
    solar_os_sketch_trace_core_stop(&trace_core);
    portEXIT_CRITICAL(&trace_lock);
}

bool solar_os_sketch_trace_active(void)
{ return __atomic_load_n(&trace_enabled, __ATOMIC_ACQUIRE); }

uint32_t solar_os_sketch_trace_new_point(bool press, uint32_t *stroke)
{
    if (!solar_os_sketch_trace_active()) return 0;
    portENTER_CRITICAL(&trace_lock);
    const uint32_t point = solar_os_sketch_trace_core_point(&trace_core, press);
    if (stroke) *stroke = trace_core.stroke;
    portEXIT_CRITICAL(&trace_lock);
    return point;
}

void solar_os_sketch_trace_record(solar_os_sketch_trace_kind_t kind,
    uint32_t point, uint32_t version, uint64_t generation, int32_t a, int32_t b)
{
    if (!solar_os_sketch_trace_active()) return;
    const int64_t begin = esp_timer_get_time();
    solar_os_sketch_trace_row_t row = {
        .kind = kind, .point = point,
        .version = version, .generation = generation, .a = a, .b = b,
    };
    portENTER_CRITICAL(&trace_lock);
    row.time_us = esp_timer_get_time();
    (void)solar_os_sketch_trace_core_append(&trace_core, row);
    portEXIT_CRITICAL(&trace_lock);
    const int64_t cost = esp_timer_get_time() - begin;
    if (cost >= 0) {
        __atomic_fetch_add(&observer_us, (uint64_t)cost, __ATOMIC_RELAXED);
        uint32_t old = __atomic_load_n(&observer_max_us, __ATOMIC_RELAXED);
        while ((uint64_t)cost > old &&
               !__atomic_compare_exchange_n(&observer_max_us, &old,
                   cost > UINT32_MAX ? UINT32_MAX : (uint32_t)cost,
                   false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
    }
}

void solar_os_sketch_trace_set_current(uint32_t point) { current_point = point; }
uint32_t solar_os_sketch_trace_current(void) { return current_point; }

size_t solar_os_sketch_trace_count(void)
{
    portENTER_CRITICAL(&trace_lock);
    const size_t count = trace_core.count;
    portEXIT_CRITICAL(&trace_lock);
    return count;
}

uint32_t solar_os_sketch_trace_overflow(void)
{
    portENTER_CRITICAL(&trace_lock);
    const uint32_t overflow = trace_core.overflow;
    portEXIT_CRITICAL(&trace_lock);
    return overflow;
}

bool solar_os_sketch_trace_row(size_t index, solar_os_sketch_trace_row_t *row)
{
    if (!row) return false;
    portENTER_CRITICAL(&trace_lock);
    const bool found = index < trace_core.count;
    if (found) *row = trace_core.rows[index];
    portEXIT_CRITICAL(&trace_lock);
    return found;
}

uint64_t solar_os_sketch_trace_observer_us(void)
{ return __atomic_load_n(&observer_us, __ATOMIC_RELAXED); }
uint32_t solar_os_sketch_trace_observer_max_us(void)
{ return __atomic_load_n(&observer_max_us, __ATOMIC_RELAXED); }
