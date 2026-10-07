#include "solar_os_status_led.h"
#include <string.h>

#include "solar_os_board_caps.h"

#include "gpio_port.h"
#include "solar_os_board.h"
#include "freertos/FreeRTOS.h"

#if SOLAR_OS_BOARD_HAS_STATUS_LED && defined(SOLAR_OS_BOARD_PIN_STATUS_LED)
#define NATIVE_LED 1
#else
#define NATIVE_LED 0
#endif

#ifndef SOLAR_OS_BOARD_STATUS_LED_ACTIVE_LEVEL
#define SOLAR_OS_BOARD_STATUS_LED_ACTIVE_LEVEL 1
#endif

#if NATIVE_LED
static bool status_led_initialized;
#endif
static bool status_led_on;
static solar_os_status_led_provider_t led_provider;
static char led_owner[24];
static portMUX_TYPE led_lock = portMUX_INITIALIZER_UNLOCKED;

static bool provider_snapshot(solar_os_status_led_provider_t *provider)
{
    portENTER_CRITICAL(&led_lock);
    *provider = led_provider;
    portEXIT_CRITICAL(&led_lock);
    return provider->set != NULL;
}

esp_err_t solar_os_status_led_register_provider(const char *owner,
    const solar_os_status_led_provider_t *provider)
{
    if (!owner || !owner[0] || strlen(owner) >= sizeof(led_owner) ||
        !provider || !provider->set || !provider->get) return ESP_ERR_INVALID_ARG;
    /* Registration itself does not alter hardware; caller sets initial OFF
     * before publication, so failure cannot expose a partially initialized LED. */
    portENTER_CRITICAL(&led_lock);
    esp_err_t err = led_owner[0] ? ESP_ERR_INVALID_STATE : ESP_OK;
    if (err == ESP_OK) { strcpy(led_owner, owner); led_provider = *provider; }
    portEXIT_CRITICAL(&led_lock);
    return err;
}

esp_err_t solar_os_status_led_unregister_provider(const char *owner)
{
    if (!owner) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&led_lock);
    esp_err_t err = strcmp(owner, led_owner) ? ESP_ERR_NOT_FOUND : ESP_OK;
    if (err == ESP_OK) { memset(&led_provider, 0, sizeof(led_provider)); led_owner[0] = 0; }
    portEXIT_CRITICAL(&led_lock);
    return err;
}

#if NATIVE_LED
static bool status_led_level(bool on)
{
    return SOLAR_OS_BOARD_STATUS_LED_ACTIVE_LEVEL ? on : !on;
}
#endif

esp_err_t solar_os_status_led_init(void)
{
    solar_os_status_led_provider_t provider;
    if (provider_snapshot(&provider)) return solar_os_status_led_set(false);
#if !NATIVE_LED
    return ESP_ERR_NOT_SUPPORTED;
#else
    const esp_err_t err =
        gpio_port_configure(SOLAR_OS_BOARD_PIN_STATUS_LED, GPIO_PORT_MODE_OUTPUT, GPIO_PORT_PULL_NONE);
    if (err != ESP_OK) {
        return err;
    }
    status_led_initialized = true;
    return solar_os_status_led_set(false);
#endif
}

esp_err_t solar_os_status_led_set(bool on)
{
    solar_os_status_led_provider_t provider;
    if (provider_snapshot(&provider)) {
        const esp_err_t err = provider.set(provider.user, on);
        if (err == ESP_OK) status_led_on = on;
        return err;
    }
#if !NATIVE_LED
    (void)on;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!status_led_initialized) {
        const esp_err_t init_err =
            gpio_port_configure(SOLAR_OS_BOARD_PIN_STATUS_LED, GPIO_PORT_MODE_OUTPUT, GPIO_PORT_PULL_NONE);
        if (init_err != ESP_OK) {
            return init_err;
        }
        status_led_initialized = true;
    }

    const esp_err_t err = gpio_port_write(SOLAR_OS_BOARD_PIN_STATUS_LED, status_led_level(on));
    if (err == ESP_OK) {
        status_led_on = on;
    }
    return err;
#endif
}

esp_err_t solar_os_status_led_toggle(bool *on_after)
{
    bool on;
    esp_err_t err = solar_os_status_led_get(&on);
    if (err != ESP_OK) return err;
    err = solar_os_status_led_set(!on);
    if (err == ESP_OK && on_after != NULL) {
        *on_after = status_led_on;
    }
    return err;
}

esp_err_t solar_os_status_led_get(bool *on)
{
    if (on == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    solar_os_status_led_provider_t provider;
    if (provider_snapshot(&provider)) return provider.get(provider.user, on);
#if !NATIVE_LED
    *on = false;
    return ESP_ERR_NOT_SUPPORTED;
#else
    *on = status_led_on;
    return ESP_OK;
#endif
}
