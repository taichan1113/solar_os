#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/spi_master.h"
#include "esp_err.h"
#include "u8g2.h"
#include "epd_ssd1677_writer_d.h"

/* Temporary, opt-in laboratory build. Not an adopted panel waveform. */
#ifndef SOLAR_OS_SSD1677_WAVEFORM_TRIAL
#define SOLAR_OS_SSD1677_WAVEFORM_TRIAL 0
#endif
#ifndef SOLAR_OS_SSD1677_HYBRID_TRIAL
#define SOLAR_OS_SSD1677_HYBRID_TRIAL 0
#endif
#ifndef SOLAR_OS_SSD1677_SPI20_TRIAL
#define SOLAR_OS_SSD1677_SPI20_TRIAL 0
#endif

/* RAM-write-only experiment. Normal firmware always uses the full-RAM path. */
#ifndef SOLAR_OS_SSD1677_DIRTY_HOLD_TRIAL
#define SOLAR_OS_SSD1677_DIRTY_HOLD_TRIAL 0
#endif

/* Opt-in SPI clock trial; normal fastest builds retain their 40 MHz default. */
#ifndef SOLAR_OS_SSD1677_SPI20_TRIAL
#define SOLAR_OS_SSD1677_SPI20_TRIAL 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EPD_SSD1677_REFRESH_AUTO = 0,
    EPD_SSD1677_REFRESH_PARTIAL,
    EPD_SSD1677_REFRESH_FULL,
    EPD_SSD1677_REFRESH_FASTEST, /* opt-in host LUT, PaperMono only */
    EPD_SSD1677_REFRESH_TYPING, /* same LUT; paced executor + idle cleanup */
#if SOLAR_OS_SSD1677_HYBRID_TRIAL
    EPD_SSD1677_REFRESH_HYBRID, /* fastest, then one OTP partial at idle */
#endif
} epd_ssd1677_refresh_mode_t;

typedef enum {
    EPD_SSD1677_WAVEFORM_BASELINE = 0,
    EPD_SSD1677_WAVEFORM_DOSE,
    EPD_SSD1677_WAVEFORM_HOLD,
} epd_ssd1677_waveform_trial_t;

typedef esp_err_t (*epd_ssd1677_power_fn_t)(void *context, bool on);
typedef esp_err_t (*epd_ssd1677_reset_fn_t)(void *context, bool high);

typedef enum {
    EPD_SSD1677_OP_PRESENT, EPD_SSD1677_OP_MODE, EPD_SSD1677_OP_READ,
    EPD_SSD1677_OP_SLEEP, EPD_SSD1677_OP_RESUME,
} epd_ssd1677_operation_t;

/* Monotonic microseconds; full has three RAM writes/two activations, fast one.
 * Write time includes tile conversion, address setup and SPI, not just wire time. */
typedef struct {
    int64_t start_us, end_us;
    int64_t write_start_us[4], write_end_us[4];
    int64_t convert_start_us[4], convert_end_us[4];
    int64_t convert_us[4], spi_us[4];
    size_t ram_data_bytes[4]; /* Successful 0x24/0x26 pixel payload per write. */
    int64_t activate_us[2], ready_us[2];
    int64_t diff_us, promote_us;
    uint8_t writes, activations;
    bool partial;
} epd_ssd1677_timing_t;

typedef struct {
    const char *spi_bus;
    spi_host_device_t spi_host;
    int sclk_pin;
    int mosi_pin;
    int cs_pin;
    int dc_pin;
    int reset_pin;
    int busy_pin;
    int power_pin;
    int spi_clock_hz;
    int busy_level;
    int power_active_level;
    const u8g2_cb_t *rotation;
    epd_ssd1677_power_fn_t set_power;
    void *power_context;
    epd_ssd1677_reset_fn_t set_reset;
    void *reset_context;
    bool papermono_otp;
} epd_ssd1677_config_t;

typedef struct {
    spi_device_handle_t spi;
    u8g2_t u8g2;
    uint8_t *buffer;
    const uint8_t *snapshot; /* worker-only source; never changes U8g2's buffer */
#if SOLAR_OS_SSD1677_WAVEFORM_TRIAL
    /* Installed/removed only while the executor is drained. No production ABI. */
    void *trial_observer_context;
    esp_err_t (*trial_observer)(void *, const uint8_t *, bool, esp_err_t);
#endif
#if SOLAR_OS_SSD1677_WRITER_D
    epd_ssd1677_writer_trial_t writer_trial;
    /* Read-only existing Executor counters, sampled only by USB diagnostics
     * under the existing operation barrier. Never called per physical update. */
    esp_err_t (*submission_status)(void *, solar_os_display_submission_status_t *);
#endif
    void *execution_context;
    void (*operation_begin)(void *, epd_ssd1677_operation_t);
    void (*operation_end)(void *, epd_ssd1677_operation_t, esp_err_t);
    uint8_t *shadow;
    uint8_t *line_buffer;
    uint8_t *band_buffer; /* PaperMono fastest, internal DMA, 32 full rows */
    size_t buffer_size;
    size_t shadow_size;
    size_t line_buffer_size;
    esp_err_t last_error;
    epd_ssd1677_refresh_mode_t refresh_mode;
    uint8_t partial_refresh_count;
    uint8_t refresh_log_count;
    int dc_pin;
    int reset_pin;
    int busy_pin;
    int power_pin;
    int busy_level;
    int power_active_level;
    epd_ssd1677_power_fn_t set_power;
    void *power_context;
    epd_ssd1677_reset_fn_t set_reset;
    void *reset_context;
    bool papermono_otp;
    bool faulted;
    /* Independent of the host shadow: valid only with continuously powered RAM. */
    bool papermono_baseline_valid;
    bool cleanup_pending;
#if SOLAR_OS_SSD1677_HYBRID_TRIAL
    bool hybrid_quality_pending;
    uint32_t hybrid_idle_ms, hybrid_max_updates, hybrid_fast_updates;
#endif
    bool timing_enabled;
    bool reference_awake; /* fastest Mode 2, analog power retained */
#if SOLAR_OS_SSD1677_DIRTY_HOLD_TRIAL
    bool dirty_hold_trial; /* RAM-only window switch; PaperMono starts in rows-dual. */
    bool dirty_hold_full_height; /* Diagnostic: X window only, full gate range. */
    bool dirty_hold_full_width; /* Diagnostic: Y window only, full source range. */
    bool dirty_hold_dual_sync; /* Trial: synchronize both command RAMs after 0x20. */
    bool dirty_hold_force_full; /* Volatile RAM window override; keeps rows-dual/LUT policy. */
    bool dirty_hold_ram_synced; /* Both controller RAM planes equal shadow. */
    uint8_t dirty_hold_dual_updates; /* Updates since the last full-RAM resync. */
#endif
    bool fastest_tiles;
    epd_ssd1677_waveform_trial_t waveform_trial; /* volatile; baseline at init */
    int configured_spi_hz, transport_spi_hz, applied_spi_hz, actual_spi_hz;
    int fastest_spi_hz;
    uint32_t typing_frame_ms, typing_cleanup_budget, typing_idle_ms;
    epd_ssd1677_timing_t timing;
    spi_host_device_t spi_host;
    bool bus_initialized;
    bool controller_ready;
    bool shadow_valid;
    bool partial_refresh_active;
    bool powered;
} epd_ssd1677_t;

esp_err_t epd_ssd1677_init(epd_ssd1677_t *display,
                           const epd_ssd1677_config_t *config);
esp_err_t epd_ssd1677_resume(epd_ssd1677_t *display);
esp_err_t epd_ssd1677_set_power_save(epd_ssd1677_t *display, bool enabled);
/* Executor owns hardware exclusively for this call. No U8g2 state is read. */
esp_err_t epd_ssd1677_present_snapshot(epd_ssd1677_t *, const uint8_t *, size_t);
#if SOLAR_OS_SSD1677_HYBRID_TRIAL
/* Worker-only: explicitly activate one same-frame OTP partial update. */
esp_err_t epd_ssd1677_present_quality_snapshot(epd_ssd1677_t *, const uint8_t *, size_t);
#endif
#if SOLAR_OS_SSD1677_WRITER_D
/* records, when non-NULL, points to SSD1677_WRITER_D_RECORDS entries. */
esp_err_t epd_ssd1677_writer_lut(epd_ssd1677_t *, const char *,
    epd_ssd1677_writer_status_t *, epd_ssd1677_writer_record_t *);
/* Same executor barrier as LUT selection; no SPI or physical activation. */
esp_err_t epd_ssd1677_ram_mode(epd_ssd1677_t *, const char *, bool *full);
#endif
void epd_ssd1677_deinit(epd_ssd1677_t *display);
u8g2_t *epd_ssd1677_get_u8g2(epd_ssd1677_t *display);
const char *epd_ssd1677_controller_mode(const epd_ssd1677_t *display);
const char *epd_ssd1677_controller_mode_values(const epd_ssd1677_t *display);
esp_err_t epd_ssd1677_set_controller_mode(epd_ssd1677_t *display,
                                          const char *mode);

#ifdef __cplusplus
}
#endif
