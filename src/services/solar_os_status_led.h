#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef struct {
    esp_err_t (*set)(void *user, bool on);
    esp_err_t (*get)(void *user, bool *on);
    void *user;
} solar_os_status_led_provider_t;
esp_err_t solar_os_status_led_register_provider(const char *owner,
    const solar_os_status_led_provider_t *provider);
esp_err_t solar_os_status_led_unregister_provider(const char *owner);

esp_err_t solar_os_status_led_init(void);
esp_err_t solar_os_status_led_set(bool on);
esp_err_t solar_os_status_led_toggle(bool *on_after);
esp_err_t solar_os_status_led_get(bool *on);
