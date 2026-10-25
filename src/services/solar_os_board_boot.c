#include "solar_os_board_boot.h"
#include "solar_os_board.h"
#include "solar_os_cdc.h"
#include "solar_os_config.h"
#if defined(SOLAR_OS_BOARD_M5PAPER_MONO)
#include "solar_os_port_shell.h"
#include "solar_os_wifi.h"
#include "esp_log.h"
#endif

esp_err_t solar_os_board_boot_start_jobs(solar_os_context_t *ctx)
{
#if defined(SOLAR_OS_BOARD_M5PAPER_MONO)
    esp_err_t result = ESP_OK;
    uint8_t session_id = 0;
    const solar_os_port_shell_options_t options = {
        .terminal_profile = SOLAR_OS_SHELL_TERMINAL_PROFILE_DUMB,
        .cols = 80, .rows = 25,
    };
    result = solar_os_port_shell_start_with_options(ctx, SOLAR_OS_CDC_PORT_NAME,
                                                   &options, false, &session_id);
    if (result != ESP_OK) ESP_LOGW("board_boot", "USB Shell unavailable: %s", esp_err_to_name(result));
#if SOLAR_OS_PACKAGE_SERVICE_WIFI
    if (solar_os_wifi_enabled_for_current_boot()) {
        esp_err_t err = solar_os_wifi_start();
        if (err == ESP_OK) err = solar_os_wifi_connect_saved();
        if (err != ESP_OK && err != ESP_ERR_NOT_FOUND)
            ESP_LOGW("board_boot", "Wi-Fi start/connect: %s", esp_err_to_name(err));
    }
#endif
    /* No NTP job, no AP/repeater enable, no new pacing or startup execution. */
    return result;
#else
    (void)ctx;
    return ESP_OK;
#endif
}
