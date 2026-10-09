#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "solar_os_expansion.h"

esp_err_t solar_os_ft6336_attach(const char *name,
                                 const solar_os_expansion_binding_t *bindings,
                                 size_t binding_count);
esp_err_t solar_os_ft6336_detach(const char *name);
esp_err_t solar_os_ft6336_set_enabled(const char *name, bool enabled);
esp_err_t solar_os_ft6336_set_suspended(const char *name, bool suspended);
bool solar_os_ft6336_is_ready(const char *name);
bool solar_os_ft6336_is_enabled(const char *name);
