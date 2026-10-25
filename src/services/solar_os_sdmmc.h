#pragma once

#include "solar_os_expansion.h"

esp_err_t solar_os_sdmmc_attach(const char *name,
                                const solar_os_expansion_binding_t *bindings,
                                size_t binding_count);
esp_err_t solar_os_sdmmc_detach(const char *name);

esp_err_t solar_os_sdmmc_set_suspended(const char *name, bool suspended);
bool solar_os_sdmmc_is_ready(const char *name);
