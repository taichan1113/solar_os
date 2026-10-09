#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "nvs.h"
#include "solar_os_battery.h"
#include "solar_os_stream.h"

static solar_os_battery_sample_t provider_sample;
static unsigned stream_register_count;
static unsigned stream_unregister_count;
static unsigned estimate_calls;
static uint16_t estimated_voltage_mv;
static uint8_t estimate_result = 42;

static uint8_t estimate_percent(void *user, uint16_t averaged_mv)
{
    assert(user == &provider_sample);
    estimate_calls++;
    estimated_voltage_mv = averaged_mv;
    return estimate_result;
}

size_t strlcpy(char *dst, const char *src, size_t size)
{
    const size_t length = strlen(src);
    if (size > 0U) {
        const size_t copy = length < size - 1U ? length : size - 1U;
        memcpy(dst, src, copy);
        dst[copy] = '\0';
    }
    return length;
}

esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *handle)
{
    (void)name;
    (void)mode;
    (void)handle;
    return ESP_ERR_NVS_NOT_FOUND;
}

esp_err_t nvs_get_u16(nvs_handle_t handle, const char *key, uint16_t *value)
{
    (void)handle;
    (void)key;
    (void)value;
    return ESP_ERR_NVS_NOT_FOUND;
}

esp_err_t nvs_get_u32(nvs_handle_t handle, const char *key, uint32_t *value)
{
    (void)handle;
    (void)key;
    (void)value;
    return ESP_ERR_NVS_NOT_FOUND;
}

esp_err_t nvs_set_u16(nvs_handle_t handle, const char *key, uint16_t value)
{
    (void)handle;
    (void)key;
    (void)value;
    return ESP_OK;
}

esp_err_t nvs_set_u32(nvs_handle_t handle, const char *key, uint32_t value)
{
    (void)handle;
    (void)key;
    (void)value;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    (void)handle;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
    (void)handle;
}

esp_err_t solar_os_stream_register(const solar_os_stream_driver_t *driver)
{
    assert(driver != NULL);
    assert(strcmp(driver->info.id, "battery") == 0);
    stream_register_count++;
    return ESP_OK;
}

esp_err_t solar_os_stream_unregister(const char *id)
{
    assert(id != NULL);
    assert(strcmp(id, "battery") == 0);
    stream_unregister_count++;
    return ESP_OK;
}

static esp_err_t read_battery(void *user, solar_os_battery_sample_t *sample)
{
    assert(user == &provider_sample);
    assert(sample != NULL);
    *sample = provider_sample;
    return ESP_OK;
}

static solar_os_battery_status_t read_status(void)
{
    solar_os_battery_status_t status;
    assert(solar_os_battery_get_status(&status) == ESP_OK);
    return status;
}

int main(void)
{
    const solar_os_battery_provider_t provider = {
        .read = read_battery,
        .user = &provider_sample,
    };
    assert(solar_os_battery_register_provider("test-battery", &provider) == ESP_OK);
    assert(solar_os_battery_init() == ESP_OK);
    assert(stream_register_count == 1U);

    provider_sample = (solar_os_battery_sample_t) {
        .battery_mv = 3750U,
        .calibrated = true,
        .external_power_valid = true,
        .external_power = true,
        .charging_valid = true,
        .charging = true,
    };
    solar_os_battery_status_t status = read_status();
    assert(status.external_power);
    assert(status.charging);
    assert(status.charging_known);
    assert(status.percent_estimated);

    provider_sample = (solar_os_battery_sample_t) {
        .battery_mv = 3750U,
        .calibrated = true,
        .percent_valid = true,
        .percent = 73U,
    };
    status = read_status();
    assert(status.percent == 73U);
    assert(!status.percent_estimated);

    provider_sample.percent = 129U;
    status = read_status();
    assert(status.percent == 100U);
    assert(!status.percent_estimated);

    provider_sample = (solar_os_battery_sample_t) {
        .battery_mv = 5000U,
        .calibrated = true,
        .external_power_valid = true,
        .external_power = false,
        .charging_valid = true,
        .charging = false,
    };
    status = read_status();
    assert(!status.external_power);
    assert(!status.charging);
    assert(status.charging_known);

    provider_sample = (solar_os_battery_sample_t) {
        .battery_mv = 5000U,
        .calibrated = true,
    };
    status = read_status();
    assert(!status.external_power);
    assert(!status.charging);
    assert(!status.charging_known);

    /* A voltage-only provider must not turn unsupported SOC into fake 100%. */
    provider_sample.percent_unavailable = true;
    provider_sample.external_power_valid = true;
    provider_sample.external_power = true;
    provider_sample.charge_complete_valid = true;
    provider_sample.charge_complete = true;
    status = read_status();
    assert(!status.percent_valid && !status.percent_estimated && status.percent == 0);
    assert(status.external_power_known && status.external_power);
    assert(status.charge_complete_known && status.charge_complete);
    provider_sample.percent_unavailable = false;
    status = read_status();
    assert(status.percent_valid && status.percent_estimated);

    assert(solar_os_battery_unregister_provider("test-battery") == ESP_OK);
    assert(stream_unregister_count == 1U);
    assert(!solar_os_battery_has_provider());

    const solar_os_battery_provider_t estimated_provider = {
        .read = read_battery,
        .user = &provider_sample,
        .estimate_percent = estimate_percent,
    };
    assert(solar_os_battery_register_provider("estimate", &estimated_provider) == ESP_OK);
    provider_sample = (solar_os_battery_sample_t){.battery_mv = 3600};
    /* Flush the existing ring; registering a provider must not add a filter. */
    for (unsigned i = 0; i < 8; i++) status = read_status();
    assert(status.voltage_mv == 3600 && estimated_voltage_mv == 3600);
    assert(status.percent_valid && status.percent_estimated && status.percent == 42);
    provider_sample.battery_mv = 4200;
    status = read_status();
    assert(status.voltage_mv == 3675 && estimated_voltage_mv == 3675);
    assert(estimate_calls == 9);
    estimate_result = 255;
    assert(read_status().percent == 100);
    const unsigned before_gauge = estimate_calls;
    provider_sample.percent_valid = true;
    provider_sample.percent = 73;
    status = read_status();
    assert(status.percent == 73 && !status.percent_estimated);
    assert(estimate_calls == before_gauge);
    provider_sample.percent_unavailable = true;
    status = read_status();
    assert(!status.percent_valid && !status.percent_estimated && status.percent == 0);
    assert(estimate_calls == before_gauge);
    assert(solar_os_battery_unregister_provider("estimate") == ESP_OK);

    puts("battery service tests: ok");
    return 0;
}
