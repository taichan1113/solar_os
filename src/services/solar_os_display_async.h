#pragma once
#include "solar_os_display_submission.h"

typedef struct solar_os_display_async solar_os_display_async_t;
typedef esp_err_t (*solar_os_display_async_execute_t)(void *, const uint8_t *, size_t, bool *);

/* Two fixed PSRAM buffers. Caller retains ownership; source must remain stable
 * during submit and may be reused immediately after submit returns. */
esp_err_t solar_os_display_async_create(size_t size, solar_os_display_async_execute_t execute,
                                      void *context, solar_os_display_async_t **out);
esp_err_t solar_os_display_async_submit(solar_os_display_async_t *, const uint8_t *, size_t, uint64_t *);
#if SOLAR_OS_SSD1677_WRITER_D
esp_err_t solar_os_display_async_submit_writer(solar_os_display_async_t *, const uint8_t *, size_t,
    uint64_t *, const solar_os_writer_frame_t *);
/* Worker callback only; maintenance deliberately has no Writer provenance. */
void solar_os_display_async_execution_origin(solar_os_display_async_t *, solar_os_writer_frame_t *);
#endif
esp_err_t solar_os_display_async_wait(solar_os_display_async_t *, uint32_t timeout_ms);
void solar_os_display_async_status(solar_os_display_async_t *, solar_os_display_submission_status_t *);
/* Serializes controls; never hold a framebuffer/render lock in the worker.
 * begin joins in-flight work; end preserves or changes persistent suspension.
 * Controls may block. Ordinary submissions never wait for panel execution. */
void solar_os_display_async_control_begin(solar_os_display_async_t *, bool discard_pending);
void solar_os_display_async_control_end(solar_os_display_async_t *, int suspended, esp_err_t fault);
/* Caller holds the control barrier. Zero values retain immediate/no-maintenance
 * behavior. Maintenance runs only after a physical-update budget and idle
 * since the LAST submission, using the completed immutable snapshot. */
void solar_os_display_async_policy(solar_os_display_async_t *, uint32_t frame_ms,
    uint32_t idle_ms, uint32_t budget, solar_os_display_async_execute_t maintenance);
/* Owner must first exclude new API callers, then destroy joins the worker. */
void solar_os_display_async_destroy(solar_os_display_async_t *);
