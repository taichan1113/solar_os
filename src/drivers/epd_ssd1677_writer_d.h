#pragma once
#include "../services/solar_os_writer_lut_trial.h"
#include "esp_err.h"
#if SOLAR_OS_SSD1677_WRITER_D
#define SSD1677_WRITER_D_RECORDS 32U
typedef struct {
    solar_os_writer_frame_t origin;
    uint64_t seq;
    uint32_t physical, ram_bytes;
    uint16_t x, y, width, height;
    bool candidate_d, candidate_e1, reference, restored;
    esp_err_t error;
    int64_t start_us, end_us, ram_start_us, ram_end_us, activation_us, ready_us;
    int64_t spi_us, restore_us, lut_us;
} epd_ssd1677_writer_record_t;
typedef struct {
    bool enabled, failed, candidate_e1;
    uint64_t attempts, d_physical, d_success, d_errors, driver_errors;
    uint64_t e1_attempts, e1_physical, e1_success, e1_errors;
    uint64_t a_physical, otp_physical, noop; /* Display-wide totals. */
    uint64_t writer_physical, writer_a, writer_otp, other_physical, writer_noop, power_controls;
    uint64_t records;
    uint8_t trace_next, trace_count;
} epd_ssd1677_writer_status_t;
typedef struct {
    /* Zero initialization selects D. Explicit Baseline persists until reselected
     * or Driver recreation/boot, without NVS writes. No cumulative stop limit. */
    bool baseline_selected, e1_selected, used_d, used_e1, reference, restored, power_control;
    solar_os_writer_frame_t origin;
    epd_ssd1677_writer_status_t status;
    epd_ssd1677_writer_record_t current, records[SSD1677_WRITER_D_RECORDS];
} epd_ssd1677_writer_trial_t;
#endif
