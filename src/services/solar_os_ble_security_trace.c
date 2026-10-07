#include "solar_os_ble_security_trace.h"
#include <stdatomic.h>
#include <inttypes.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "solar_os_log.h"

#define CAPACITY 512U
typedef struct {
    const char *event;
    int64_t time_us;
    uintptr_t proc;
    uint32_t generation;
    uint16_t conn;
    uint8_t state, flags;
    int a, b;
} trace_entry_t;
static trace_entry_t *entries;
static atomic_uint head, tail, dropped, foreign_producer;
static atomic_uintptr_t producer;
static uint32_t generation;
static struct { uint16_t conn; uint32_t generation; } generations[16];

void solar_os_ble_security_trace_init(void)
{
    if (entries) {
        /* backend init is serialized after the previous host has stopped. */
        atomic_store(&producer, 0);
        return;
    }
    entries = heap_caps_calloc(CAPACITY, sizeof(*entries), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!entries) SOLAR_OS_LOGW("ble_security", "diagnostic trace allocation failed");
}

void solar_os_ble_security_trace_record(const char *event, uint16_t conn,
    uintptr_t proc, uint8_t state, uint8_t flags, int a, int b)
{
    if (!entries) return;
    /* These SDK paths run on the single NimBLE host task. Fail visibly rather
     * than use an unsafe SPSC queue if a different caller is introduced. */
    uintptr_t caller = (uintptr_t)xTaskGetCurrentTaskHandle();
    uintptr_t empty = 0;
    atomic_compare_exchange_strong(&producer, &empty, caller);
    if (atomic_load(&producer) != caller) { atomic_fetch_add(&foreign_producer, 1); return; }
    unsigned slot = conn % 16U;
    if (!strcmp(event, "sm_insert")) {
        generations[slot].conn = conn;
        generations[slot].generation = ++generation;
    }
    unsigned write = atomic_load_explicit(&head, memory_order_relaxed);
    unsigned read = atomic_load_explicit(&tail, memory_order_acquire);
    if (write - read >= CAPACITY) { atomic_fetch_add(&dropped, 1); return; }
    entries[write % CAPACITY] = (trace_entry_t){
        .event=event, .time_us=esp_timer_get_time(), .proc=proc,
        .generation=generations[slot].conn == conn ? generations[slot].generation : 0,
        .conn=conn, .state=state, .flags=flags, .a=a, .b=b};
    atomic_store_explicit(&head, write + 1, memory_order_release);
}

void solar_os_ble_security_trace_drain(void)
{
    if (!entries) return;
    for (unsigned count = 0; count < 16; ++count) {
        unsigned read = atomic_load_explicit(&tail, memory_order_relaxed);
        if (read == atomic_load_explicit(&head, memory_order_acquire)) break;
        trace_entry_t e = entries[read % CAPACITY];
        atomic_store_explicit(&tail, read + 1, memory_order_release);
        SOLAR_OS_LOGD("ble_security", "seq=%u t=%" PRId64 " gen=%" PRIu32
            " conn=%u proc=%" PRIxPTR " %s state=%u flags=%u a=%d b=%d",
            read + 1, e.time_us, e.generation, e.conn, e.proc,
            e.event, e.state, e.flags, e.a, e.b);
    }
    unsigned lost = atomic_exchange(&dropped, 0);
    unsigned foreign = atomic_exchange(&foreign_producer, 0);
    if (lost || foreign) SOLAR_OS_LOGW("ble_security", "trace lost=%u foreign_producer=%u", lost, foreign);
}
