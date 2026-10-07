#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define FT6336_ADDRESS 0x38U

typedef struct {
    bool touched;
    uint8_t id;
    uint16_t x;
    uint16_t y;
} ft6336_sample_t;

esp_err_t ft6336_init(const char *i2c_bus,
                      uint8_t address,
                      int reset_pin,
                      int irq_pin);
typedef esp_err_t (*ft6336_reset_fn_t)(void *context, bool high);
esp_err_t ft6336_init_with_reset(const char *i2c_bus, uint8_t address,
                                int irq_pin, ft6336_reset_fn_t reset, void *context);
esp_err_t ft6336_read(ft6336_sample_t *sample);
void ft6336_deinit(void);
esp_err_t ft6336_disable_irq(int irq_pin);
