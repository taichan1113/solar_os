#pragma once
#include "../services/solar_os_writer_lut_trial.h"
#include "../services/solar_os_display_submission.h"
#include "esp_err.h"
#if SOLAR_OS_SSD1677_WRITER_D
#define SSD1677_WRITER_D_RECORDS 32U
typedef enum {
    SSD1677_RAM_NONE, SSD1677_RAM_FULL, SSD1677_RAM_ROWS, SSD1677_RAM_RECT, SSD1677_RAM_OTP
} epd_ssd1677_ram_path_t;

typedef struct { uint64_t count, sum_us, min_us, max_us; } epd_ssd1677_trial_metric_t;
typedef struct {
    bool started, active, overflow, queue_valid, queue_wrapped, pending_start, pending_end;
    int64_t start_us, end_us;
    uint64_t first_seq, last_seq, frames, physical, a, d, e1, f, otp;
    uint64_t writer, sketch, other, errors, noop, full_ram, rows_ram, rect_ram, otp_ram, periodic_resync;
    uint64_t ram_bytes, timing_missing, submitted, replaced, cancelled, rejected, writer_coalesced;
    epd_ssd1677_trial_metric_t busy, start_gap, ready_gap;
} epd_ssd1677_trial_summary_t;
typedef struct {
    solar_os_writer_frame_t origin;
    uint64_t seq;
    uint64_t activation_first_seq, activation_last_seq;
    uint32_t physical, ram_bytes;
    uint16_t x, y, width, height;
    uint16_t ram_x, ram_y, ram_width, ram_height;
    epd_ssd1677_ram_path_t ram_path;
    bool timing_valid, periodic_resync;
    bool candidate_d, candidate_e1, candidate_f, reference, restored;
    esp_err_t error;
    int64_t start_us, end_us, ram_start_us, ram_end_us, activation_us, ready_us;
    int64_t spi_us, restore_us, lut_us;
    int64_t busy_us, start_gap_us, ready_gap_us;
} epd_ssd1677_writer_record_t;
typedef struct {
    bool enabled, failed, candidate_e1, candidate_f, candidate_d_sketch;
    uint64_t attempts, d_physical, d_success, d_errors, driver_errors;
    uint64_t e1_attempts, e1_physical, e1_success, e1_errors;
    uint64_t f_attempts, f_physical, f_success, f_errors;
    uint64_t a_physical, otp_physical, noop; /* Display-wide totals. */
    uint64_t writer_physical, writer_a, writer_otp, other_physical, writer_noop, power_controls;
    uint64_t records;
    uint8_t trace_next, trace_count;
    epd_ssd1677_trial_summary_t trial;
} epd_ssd1677_writer_status_t;
typedef struct {
    /* Zero initialization selects D. Explicit Baseline persists until reselected
     * or Driver recreation/boot, without NVS writes. No cumulative stop limit. */
    bool baseline_selected, e1_selected, f_selected, sketch_d_selected;
    bool used_d, used_e1, used_f, reference, restored, power_control;
    bool last_activation_valid, last_ready_valid;
    int64_t last_activation_us, last_ready_us;
    uint64_t activation_sequence;
    /* Interval pairs only: never import the pre-trial gap into the average. */
    bool trial_last_activation_valid, trial_last_ready_valid;
    int64_t trial_last_activation_us, trial_last_ready_us;
    solar_os_display_submission_status_t trial_queue_start;
    solar_os_writer_frame_t origin;
    epd_ssd1677_writer_status_t status;
    epd_ssd1677_writer_record_t current, records[SSD1677_WRITER_D_RECORDS];
} epd_ssd1677_writer_trial_t;
#endif
