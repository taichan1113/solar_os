#include "solar_os_display_async.h"
#if defined(SOLAR_OS_SKETCH_LATENCY_TRIAL) && SOLAR_OS_SKETCH_LATENCY_TRIAL
#include "solar_os_sketch_trace.h"
#endif
#include <inttypes.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "solar_os_task.h"
#include "solar_os_perf.h"

struct solar_os_display_async {
    SemaphoreHandle_t state, control;
    TaskHandle_t task;
    uint8_t *buffers[2];
    int pending_slot, active_slot;
#if SOLAR_OS_SSD1677_WRITER_D_TRIAL
    solar_os_writer_frame_t origins[2], executing_origin;
#endif
    int completed_slot;
    uint32_t frame_ms, idle_ms, budget, updates;
    int64_t next_frame_us, last_submit_us;
    solar_os_display_async_execute_t maintenance;
    uint64_t pending_id;
    bool controlled, reject_submits, stopping, done;
    size_t size;
    void *context;
    solar_os_display_async_execute_t execute;
    solar_os_display_submission_status_t status;
};

static void lock(solar_os_display_async_t *a) { xSemaphoreTake(a->state, portMAX_DELAY); }
static void unlock(solar_os_display_async_t *a) { xSemaphoreGive(a->state); }

static void discard_pending(solar_os_display_async_t *a)
{
    if (a->pending_slot < 0) return;
    a->status.cancelled++;
#if SOLAR_OS_SSD1677_WRITER_D_TRIAL
    if (a->origins[a->pending_slot].is_writer) a->status.writer_cancelled++;
#endif
    a->status.cancelled_id = a->pending_id;
    a->pending_slot = -1;
    a->status.pending = false;
}

static void worker(void *arg)
{
    solar_os_display_async_t *a = arg;
    for (;;) {
        lock(a);
        if (a->stopping) {
            a->done = true;
            unlock(a);
            solar_os_task_delete_internal(NULL);
            return;
        }
        const int64_t now = esp_timer_get_time();
        int64_t deadline = 0;
        bool maintenance = false, runnable = false;
        if (!a->controlled && !a->status.suspended && a->status.error == ESP_OK) {
            if (a->pending_slot >= 0) {
                deadline = a->next_frame_us;
                runnable = now >= deadline;
            } else if (a->maintenance && a->budget && a->idle_ms &&
                       a->updates >= a->budget && a->completed_slot >= 0) {
                deadline = a->last_submit_us + (int64_t)a->idle_ms * 1000;
                runnable = now >= deadline;
                maintenance = true;
            }
        }
        if (!runnable) {
            TickType_t timeout = portMAX_DELAY;
            if (deadline > now) {
                const uint32_t ms = (uint32_t)((deadline-now+999)/1000);
                timeout = pdMS_TO_TICKS(ms);
                if (!timeout) timeout = 1;
            }
            unlock(a);
            ulTaskNotifyTake(pdTRUE, timeout);
            continue;
        }
        a->active_slot = maintenance ? a->completed_slot : a->pending_slot;
        const uint64_t id = maintenance ? a->status.completed_id : a->pending_id;
        if (!maintenance) a->pending_slot = -1;
        const uint8_t *frame = a->buffers[a->active_slot];
#if SOLAR_OS_SSD1677_WRITER_D_TRIAL
        a->executing_origin = maintenance ? (solar_os_writer_frame_t){0} : a->origins[a->active_slot];
#endif
        if (!maintenance) a->status.pending = false;
        a->status.busy = true;
        if (maintenance) a->status.maintenance_started++; else a->status.started++;
        const solar_os_display_async_execute_t execute = maintenance ? a->maintenance : a->execute;
        unlock(a);
#if defined(SOLAR_OS_SKETCH_LATENCY_TRIAL) && SOLAR_OS_SKETCH_LATENCY_TRIAL
        if (!maintenance) solar_os_sketch_trace_record(SKTRACE_WORKER_START,
            0, 0, id, 0, 0);
#endif
        const bool perf = solar_os_perf_enabled();
        if (perf && !maintenance) SOLAR_OS_LOGD("display_async", "gen=%" PRIu64 " WORKER_START t=%" PRId64, id, esp_timer_get_time());
        bool physical = false;
        const esp_err_t err = execute(a->context, frame, a->size, &physical);
        const int64_t end = perf ? esp_timer_get_time() : 0;
        lock(a);
        a->status.busy = false;
        if (err == ESP_OK) {
            a->completed_slot = a->active_slot;
            if (maintenance) {
                a->status.maintenance_completed++;
                a->updates = 0;
            } else {
                a->status.completed_id = id;
                a->status.completed++;
                if (physical && a->updates != UINT32_MAX) a->updates++;
            }
            if (physical) a->status.physical++;
            if (physical) a->next_frame_us = now + (int64_t)a->frame_ms * 1000;
        } else {
            a->status.error = err;
            a->completed_slot = -1;
            discard_pending(a);
        }
        a->active_slot = -1;
        unlock(a);
#if defined(SOLAR_OS_SKETCH_LATENCY_TRIAL) && SOLAR_OS_SKETCH_LATENCY_TRIAL
        if (!maintenance) solar_os_sketch_trace_record(SKTRACE_WORKER_END,
            0, 0, id, err, physical);
#endif
        if (perf && !maintenance) SOLAR_OS_LOGD("display_async", "gen=%" PRIu64 " WORKER_END t=%" PRId64 " err=%d physical=%u", id, end, err, physical);
        if (err != ESP_OK) SOLAR_OS_LOGE("display_async", "refresh failed: %d; pending discarded, submissions latched", err);
    }
}

esp_err_t solar_os_display_async_create(size_t size, solar_os_display_async_execute_t execute,
                                      void *context, solar_os_display_async_t **out)
{
    if (!out || !execute || !size) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    solar_os_display_async_t *a = heap_caps_calloc(1, sizeof(*a), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!a) return ESP_ERR_NO_MEM;
    a->size = size;
    a->context = context;
    a->execute = execute;
    a->active_slot = a->pending_slot = a->completed_slot = -1;
    a->state = xSemaphoreCreateMutex();
    a->control = xSemaphoreCreateMutex();
    a->buffers[0] = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    a->buffers[1] = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!a->state || !a->control || !a->buffers[0] || !a->buffers[1]) goto fail;
    a->status.enabled = true;
    a->status.snapshot_bytes = size * 2;
    if (solar_os_task_create_pinned_internal(worker, "display_async", 6144, a, 1,
            &a->task, tskNO_AFFINITY, SOLAR_OS_TASK_ROLE_SYSTEM) != pdPASS) goto fail;
    *out = a;
    return ESP_OK;
fail:
    if (a->state) vSemaphoreDelete(a->state);
    if (a->control) vSemaphoreDelete(a->control);
    heap_caps_free(a->buffers[0]);
    heap_caps_free(a->buffers[1]);
    heap_caps_free(a);
    return ESP_ERR_NO_MEM;
}

static esp_err_t submit_snapshot(solar_os_display_async_t *a, const uint8_t *src,
                                       size_t size, uint64_t *id
#if SOLAR_OS_SSD1677_WRITER_D_TRIAL
                                       , const solar_os_writer_frame_t *origin
#endif
                                       )
{
    if (!a || !src || size != a->size) return ESP_ERR_INVALID_ARG;
    const bool perf = solar_os_perf_enabled();
    const int64_t start = perf ? esp_timer_get_time() : 0;
    lock(a);
    if (a->stopping || a->reject_submits || a->status.suspended || a->status.error != ESP_OK) {
        const esp_err_t err = a->status.error != ESP_OK ? a->status.error : ESP_ERR_INVALID_STATE;
        a->status.rejected++;
        unlock(a);
        return err;
    }
    const uint64_t replaced = a->pending_slot >= 0 ? a->pending_id : 0;
    const int slot = a->pending_slot >= 0 ? a->pending_slot : (a->active_slot == 0 ? 1 : 0);
#if SOLAR_OS_SSD1677_WRITER_D_TRIAL
    solar_os_writer_frame_t next = origin ? *origin : (solar_os_writer_frame_t){0};
    const int64_t queued = esp_timer_get_time();
    next.queued_us = next.queued_first_us = queued;
    if (next.is_writer) a->status.writer_submitted++;
    if (replaced && a->origins[slot].is_writer) {
        if (next.is_writer) {
            const solar_os_writer_frame_t *old = &a->origins[slot];
            if (old->input_first_us && (!next.input_first_us || old->input_first_us < next.input_first_us))
                next.input_first_us = old->input_first_us;
            if (old->input_last_us > next.input_last_us) next.input_last_us = old->input_last_us;
            next.inputs += old->inputs;
            next.coalesced += old->coalesced + 1;
            next.queued_first_us = old->queued_first_us;
            a->status.writer_coalesced++;
        } else a->status.writer_superseded++;
    }
    a->origins[slot] = next;
#endif
    memcpy(a->buffers[slot], src, size);
    a->pending_slot = slot;
    a->pending_id = ++a->status.submitted_id;
    const uint64_t generation = a->pending_id;
#if SOLAR_OS_SSD1677_WRITER_D_TRIAL
    a->origins[slot].generation = generation;
#endif
    a->status.submitted++;
    if (replaced) a->status.replaced++;
    a->status.pending = true;
    a->last_submit_us = esp_timer_get_time();
    const unsigned outstanding = a->status.busy ? 2 : 1;
    if (outstanding > a->status.peak_outstanding) a->status.peak_outstanding = outstanding;
    unlock(a);
#if defined(SOLAR_OS_SKETCH_LATENCY_TRIAL) && SOLAR_OS_SKETCH_LATENCY_TRIAL
    if (replaced) solar_os_sketch_trace_record(SKTRACE_REPLACE,
        0, 0, generation, (int32_t)replaced, 0);
#endif
    if (id) *id = generation;
    if (perf) SOLAR_OS_LOGD("display_async", "gen=%" PRIu64 " SNAPSHOT_SUBMIT t=%" PRId64 " copied_us=%" PRId64 " replaced=%" PRIu64,
                          generation, start, esp_timer_get_time() - start, replaced);
    xTaskNotifyGive(a->task);
    return ESP_OK;
}

esp_err_t solar_os_display_async_submit(solar_os_display_async_t *a, const uint8_t *src,
    size_t size, uint64_t *id)
{
    return submit_snapshot(a, src, size, id
#if SOLAR_OS_SSD1677_WRITER_D_TRIAL
        , NULL
#endif
    );
}
#if SOLAR_OS_SSD1677_WRITER_D_TRIAL
esp_err_t solar_os_display_async_submit_writer(solar_os_display_async_t *a, const uint8_t *src,
    size_t size, uint64_t *id, const solar_os_writer_frame_t *origin)
{
    if (!origin || !origin->is_writer) return ESP_ERR_INVALID_ARG;
    return submit_snapshot(a, src, size, id, origin);
}
void solar_os_display_async_execution_origin(solar_os_display_async_t *a, solar_os_writer_frame_t *origin)
{
    lock(a); *origin = a->executing_origin; unlock(a);
}
#endif

void solar_os_display_async_status(solar_os_display_async_t *a, solar_os_display_submission_status_t *s)
{
    if (!a || !s) return;
    lock(a);
    *s = a->status;
    unlock(a);
}

esp_err_t solar_os_display_async_wait(solar_os_display_async_t *a, uint32_t timeout_ms)
{
    if (!a) return ESP_ERR_INVALID_ARG;
    solar_os_display_submission_status_t s;
    solar_os_display_async_status(a, &s);
    const uint64_t target = s.submitted_id;
    const int64_t start = esp_timer_get_time();
    for (;;) {
        solar_os_display_async_status(a, &s);
        if (s.error != ESP_OK) return s.error;
        if (s.completed_id >= target) return ESP_OK;
        if (s.cancelled_id >= target && !s.busy) return ESP_ERR_INVALID_STATE;
        if (esp_timer_get_time() - start >= (int64_t)timeout_ms * 1000) return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void solar_os_display_async_control_begin(solar_os_display_async_t *a, bool discard)
{
    if (!a) return;
    xSemaphoreTake(a->control, portMAX_DELAY);
    lock(a);
    a->controlled = true;
    a->reject_submits = discard;
    if (discard) {
        discard_pending(a);
        a->completed_slot = -1;
        a->updates = 0;
    }
    unlock(a);
    for (;;) {
        lock(a);
        const bool busy = a->status.busy;
        if (!busy && discard) { a->completed_slot = -1; a->updates = 0; }
        unlock(a);
        if (!busy) return;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void solar_os_display_async_control_end(solar_os_display_async_t *a, int suspended, esp_err_t fault)
{
    if (!a) return;
    lock(a);
    if (suspended >= 0) {
        a->status.suspended = suspended != 0;
        if (suspended) { a->completed_slot = -1; a->updates = 0; }
    }
    if (fault != ESP_OK) { a->status.error = fault; discard_pending(a); }
    a->controlled = false;
    a->reject_submits = false;
    unlock(a);
    xTaskNotifyGive(a->task);
    xSemaphoreGive(a->control);
}

void solar_os_display_async_policy(solar_os_display_async_t *a, uint32_t frame_ms,
    uint32_t idle_ms, uint32_t budget, solar_os_display_async_execute_t maintenance)
{
    if (!a) return;
    lock(a);
    a->frame_ms = frame_ms;
    a->idle_ms = idle_ms;
    a->budget = budget;
    a->maintenance = maintenance;
    a->updates = 0;
    a->next_frame_us = 0;
    unlock(a);
}

void solar_os_display_async_destroy(solar_os_display_async_t *a)
{
    if (!a) return;
    solar_os_display_async_control_begin(a, true);
    lock(a);
    a->stopping = true;
    unlock(a);
    xTaskNotifyGive(a->task);
    for (;;) {
        lock(a);
        const bool done = a->done;
        unlock(a);
        if (done) break;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    xSemaphoreGive(a->control);
    vSemaphoreDelete(a->control);
    vSemaphoreDelete(a->state);
    heap_caps_free(a->buffers[0]);
    heap_caps_free(a->buffers[1]);
    heap_caps_free(a);
}
