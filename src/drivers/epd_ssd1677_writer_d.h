#pragma once
#include "../services/solar_os_writer_lut_trial.h"
#include "esp_err.h"
#if SOLAR_OS_SSD1677_WRITER_D_TRIAL
#define SSD1677_WRITER_D_LIMIT 6U
#define SSD1677_WRITER_D_RECORDS 32U
typedef struct {
    solar_os_writer_frame_t origin;
    uint32_t seq, physical, ram_bytes;
    uint16_t x, y, width, height;
    bool candidate_d, reference, restored, cap_transition;
    esp_err_t error;
    int64_t start_us, end_us, ram_start_us, ram_end_us, activation_us, ready_us;
    int64_t spi_us, restore_us, lut_us;
} epd_ssd1677_writer_record_t;
typedef struct {
    bool enabled, failed, cap_reached;
    uint32_t attempts, d_physical, d_success, d_errors;
    uint32_t writer_physical, writer_a, writer_otp, other_physical, writer_noop, power_controls;
    uint32_t records;
} epd_ssd1677_writer_status_t;
typedef struct {
    bool enabled, used_d, reference, restored, cap_transition, power_control;
    solar_os_writer_frame_t origin;
    epd_ssd1677_writer_status_t status;
    epd_ssd1677_writer_record_t current, records[SSD1677_WRITER_D_RECORDS];
} epd_ssd1677_writer_trial_t;
#endif
