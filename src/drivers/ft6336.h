#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "ft6336_frame.h"

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
/* One 13-byte burst via the existing I2C service. ESP_OK means transport success;
 * check frame->result before using contacts. On argument/state/I2C error, frame
 * is untouched; callers must NOT reuse it as a fresh frame or infer release. */
esp_err_t ft6336_read_frame(ft6336_frame_t *frame);
void ft6336_deinit(void);
esp_err_t ft6336_disable_irq(int irq_pin);
