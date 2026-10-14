#pragma once

#include "esp_err.h"
#include "ft6336_frame.h"
#include "solar_os_port.h"

#define SOLAR_OS_TOUCHRAW_CAPACITY 4096U
#define SOLAR_OS_TOUCHRAW_TIMEOUT_US 30000000LL

typedef enum {
    TOUCHRAW_IDLE, TOUCHRAW_PREPARING, TOUCHRAW_PENDING,
    TOUCHRAW_RUNNING, TOUCHRAW_WAIT_RELEASE
} solar_os_touchraw_mode_t;
typedef enum {
    TOUCHRAW_END_NONE, TOUCHRAW_END_STOP, TOUCHRAW_END_TIMEOUT,
    TOUCHRAW_END_DISCONNECT, TOUCHRAW_END_IO, TOUCHRAW_END_FULL,
    TOUCHRAW_END_SOURCE, TOUCHRAW_END_START_FAILED
} solar_os_touchraw_end_t;

/* Failed reads have error != ESP_OK and no raw/decoder measurement. */
typedef struct {
    int64_t start_us, end_us;
    uint32_t sequence;
    int32_t error;
    uint32_t reasons;
    uint8_t result;
    uint8_t raw[FT6336_FRAME_BYTES];
} solar_os_touchraw_record_t;

typedef struct {
    solar_os_touchraw_mode_t mode;
    solar_os_touchraw_end_t end;
    uint32_t reads, stored, dropped, failures, consecutive, max_consecutive;
    uint32_t results[3], counts[4], flags[4], reported_flags[4];
    uint32_t sentinel_slots, reported_sentinel, duplicates;
    uint64_t logger_total_us;
    uint32_t logger_max_us;
    int64_t started_us;
    bool dumping;
} solar_os_touchraw_status_t;

/* USB diagnostics only; touch must already be enabled. Journal must be
 * explicitly cleared before another acquisition, never silently overwritten.
 * Requests are consumed by the existing FT6336 worker, the sole I2C owner. */
esp_err_t solar_os_ft6336_raw_start(const solar_os_port_handle_t *owner);
esp_err_t solar_os_ft6336_raw_stop(void);
esp_err_t solar_os_ft6336_raw_clear(void);
void solar_os_ft6336_raw_status(solar_os_touchraw_status_t *status);
/* A dump lease freezes the journal against start/clear. No active dump. */
esp_err_t solar_os_ft6336_raw_dump_begin(solar_os_touchraw_status_t *status);
esp_err_t solar_os_ft6336_raw_record(size_t index, solar_os_touchraw_record_t *record);
void solar_os_ft6336_raw_dump_end(void);
