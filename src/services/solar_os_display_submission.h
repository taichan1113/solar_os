#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* Submission acceptance and physical completion are different fences. */
typedef struct {
    uint64_t submitted_id, completed_id, cancelled_id;
    uint32_t submitted, replaced, cancelled, started, completed, physical, rejected;
    uint32_t maintenance_started, maintenance_completed;
    size_t snapshot_bytes;
    unsigned peak_outstanding;
    bool enabled, busy, pending, suspended;
    esp_err_t error;
} solar_os_display_submission_status_t;

typedef struct {
    void *context;
    esp_err_t (*submit)(void *, const uint8_t *, size_t, uint64_t *);
    esp_err_t (*wait)(void *, uint32_t);
    esp_err_t (*status)(void *, solar_os_display_submission_status_t *);
} solar_os_display_submission_t;
