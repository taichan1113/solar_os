#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "solar_os_bus_types.h"

typedef struct {
    void *context;
    esp_err_t (*read_reg)(void *, uint8_t, uint8_t, uint8_t *, size_t);
    esp_err_t (*write_reg)(void *, uint8_t, uint8_t, const uint8_t *, size_t);
} solar_os_i2c_transaction_t;
esp_err_t solar_os_bus_i2c_transaction(const char *,
    esp_err_t (*)(const solar_os_i2c_transaction_t *, void *), void *);

esp_err_t solar_os_bus_i2c_read_reg(const char *name,
                                    uint8_t address,
                                    uint8_t reg,
                                    uint8_t *data,
                                    size_t len);
esp_err_t solar_os_bus_i2c_write_reg(const char *name,
                                     uint8_t address,
                                     uint8_t reg,
                                     const uint8_t *data,
                                     size_t len);
