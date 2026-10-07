#include "ft6336.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "solar_os_buses.h"

#define FT6336_REG_TOUCH_STATUS 0x02U
#define FT6336_REG_CHIP_ID 0xa3U

static const char *TAG = "ft6336";
static bool ft6336_ready;
static char ft6336_i2c_bus[SOLAR_OS_BUS_NAME_MAX];
static uint8_t ft6336_address;

esp_err_t ft6336_init_with_reset(const char *i2c_bus, uint8_t address,
                                int irq_pin, ft6336_reset_fn_t reset, void *context)
{
    if (i2c_bus == NULL || i2c_bus[0] == '\0' ||
        address != FT6336_ADDRESS ||
        reset == NULL || irq_pin < 0 || irq_pin >= GPIO_NUM_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (ft6336_ready) {
        return strcmp(ft6336_i2c_bus, i2c_bus) == 0 &&
                ft6336_address == address
            ? ESP_OK
            : ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(reset(context, false),
                        TAG,
                        "reset low failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(reset(context, true),
                        TAG,
                        "reset high failed");
    vTaskDelay(pdMS_TO_TICKS(500));

    const gpio_config_t input = {
        .pin_bit_mask = 1ULL << (uint32_t)irq_pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&input), TAG, "interrupt pin config failed");

    uint8_t chip_id = 0;
    ESP_RETURN_ON_ERROR(solar_os_bus_i2c_read_reg(i2c_bus,
                                                 address,
                                                 FT6336_REG_CHIP_ID,
                                                 &chip_id,
                                                 sizeof(chip_id)),
                        TAG,
                        "chip ID read failed");
    strlcpy(ft6336_i2c_bus, i2c_bus, sizeof(ft6336_i2c_bus));
    ft6336_address = address;
    ft6336_ready = true;
    return ESP_OK;
}

static esp_err_t native_reset(void *context, bool high)
{
    return gpio_set_level(*(int *)context, high);
}

esp_err_t ft6336_init(const char *i2c_bus, uint8_t address, int reset_pin, int irq_pin)
{
    if (reset_pin < 0 || reset_pin >= GPIO_NUM_MAX) return ESP_ERR_INVALID_ARG;
    const gpio_config_t output = {
        .pin_bit_mask = 1ULL << (uint32_t)reset_pin, .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&output), TAG, "reset pin config failed");
    return ft6336_init_with_reset(i2c_bus, address, irq_pin, native_reset, &reset_pin);
}

esp_err_t ft6336_read(ft6336_sample_t *sample)
{
    if (sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ft6336_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t data[6] = {0};
    ESP_RETURN_ON_ERROR(solar_os_bus_i2c_read_reg(ft6336_i2c_bus,
                                                 ft6336_address,
                                                 FT6336_REG_TOUCH_STATUS,
                                                 data,
                                                 sizeof(data)),
                        TAG,
                        "touch read failed");
    memset(sample, 0, sizeof(*sample));
    const uint8_t count = data[0] & 0x0fU;
    if (count == 0) {
        return ESP_OK;
    }

    sample->touched = true;
    sample->x = (uint16_t)(((uint16_t)(data[1] & 0x0fU) << 8) | data[2]);
    sample->y = (uint16_t)(((uint16_t)(data[3] & 0x0fU) << 8) | data[4]);
    sample->id = data[3] >> 4;
    return ESP_OK;
}

void ft6336_deinit(void)
{
    ft6336_ready = false;
    ft6336_i2c_bus[0] = '\0';
    ft6336_address = 0;
}

esp_err_t ft6336_disable_irq(int irq_pin)
{
    if (irq_pin < 0 || irq_pin >= GPIO_NUM_MAX) return ESP_ERR_INVALID_ARG;
    const gpio_config_t off = {
        .pin_bit_mask = 1ULL << (uint32_t)irq_pin, .mode = GPIO_MODE_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&off);
}
