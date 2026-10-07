#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/spi_master.h"
#include "esp_err.h"
#include "u8g2.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EPD_SSD1677_REFRESH_AUTO = 0,
    EPD_SSD1677_REFRESH_PARTIAL,
    EPD_SSD1677_REFRESH_FULL,
    EPD_SSD1677_REFRESH_FASTEST, /* opt-in host LUT, PaperMono only */
    EPD_SSD1677_REFRESH_BALANCED, /* absolute endpoints, PaperMono only */
} epd_ssd1677_refresh_mode_t;

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
    int64_t write_start_us[3], write_end_us[3];
    int64_t convert_start_us[3], convert_end_us[3];
    int64_t convert_us[3], spi_us[3];
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
    bool timing_enabled;
    bool reference_awake; /* fastest Mode 2, analog power retained */
    bool fastest_tiles;
    int configured_spi_hz, transport_spi_hz, applied_spi_hz, actual_spi_hz;
    int fastest_spi_hz;
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
void epd_ssd1677_deinit(epd_ssd1677_t *display);
u8g2_t *epd_ssd1677_get_u8g2(epd_ssd1677_t *display);
const char *epd_ssd1677_controller_mode(const epd_ssd1677_t *display);
const char *epd_ssd1677_controller_mode_values(const epd_ssd1677_t *display);
esp_err_t epd_ssd1677_set_controller_mode(epd_ssd1677_t *display,
                                          const char *mode);

#ifdef __cplusplus
}
#endif
