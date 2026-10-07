#include "rtc_rx8130ce.h"
#include "solar_os_expansion.h"
#include "solar_os_rtc.h"
#include "esp_log.h"
#include <string.h>

static struct {
    rtc_rx8130ce_t rtc;
    bool active;
    char name[SOLAR_OS_EXPANSION_DEVICE_NAME_MAX];
} device;

static esp_err_t get(void *user, solar_os_datetime_t *dt)
{
    (void)user;
    return device.active ? rtc_rx8130ce_get(&device.rtc, dt) : ESP_ERR_INVALID_STATE;
}
static esp_err_t set(void *user, const solar_os_datetime_t *dt)
{
    (void)user;
    if (!device.active) return ESP_ERR_INVALID_STATE;
    esp_err_t err = rtc_rx8130ce_set(&device.rtc, dt);
    if (err == ESP_OK) err = solar_os_time_sync_system_from_provider();
    return err;
}
static esp_err_t attach(const char *name, const solar_os_expansion_binding_t *b, size_t n)
{
    if (!name || !name[0] || strlen(name) >= sizeof(device.name) || !b || device.active)
        return ESP_ERR_INVALID_ARG;
    const char *bus = NULL;
    int addr = -1;
    for (size_t i = 0; i < n; i++) {
        if (b[i].kind == SOLAR_OS_EXPANSION_BINDING_I2C_BUS && !bus) bus = b[i].target;
        else if (b[i].kind == SOLAR_OS_EXPANSION_BINDING_I2C_ADDRESS && addr < 0) addr = b[i].value;
        else return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = rtc_rx8130ce_init(&device.rtc, bus, addr);
    if (err != ESP_OK) return err;
    strlcpy(device.name, name, sizeof(device.name));
    device.active = true;
    const solar_os_rtc_provider_t provider = {
        .get_utc_datetime = get, .set_utc_datetime = set,
        .interrupt_gpio = SOLAR_OS_RTC_INTERRUPT_GPIO_NONE,
    };
    err = solar_os_rtc_register_provider(name, &provider);
    if (err != ESP_OK) { memset(&device, 0, sizeof(device)); return err; }
    const esp_err_t sync_err = solar_os_time_sync_system_from_provider();
    if (sync_err != ESP_OK) ESP_LOGW("rx8130ce", "RTC invalid/unavailable; boot continues: %s", esp_err_to_name(sync_err));
    else ESP_LOGI("rx8130ce", "UTC restored from RTC");
    return ESP_OK;
}
static esp_err_t detach(const char *name)
{
    if (!device.active || !name || strcmp(name, device.name)) return ESP_ERR_NOT_FOUND;
    esp_err_t err = solar_os_rtc_unregister_provider(name);
    if (err == ESP_OK) memset(&device, 0, sizeof(device));
    return err;
}
static const int addresses[] = {RTC_RX8130CE_ADDRESS};
static const solar_os_expansion_binding_spec_t specs[] = {
    {.key = "i2c", .value_hint = "bus", .kind = SOLAR_OS_EXPANSION_BINDING_I2C_BUS, .required = true},
    {.key = "addr", .value_hint = "0x32", .kind = SOLAR_OS_EXPANSION_BINDING_I2C_ADDRESS,
     .required = true, .allowed_values = addresses, .allowed_value_count = 1},
};
const solar_os_expansion_driver_t solar_os_rx8130ce_expansion_driver = {
    .name = "rx8130ce", .summary = "RX8130CE UTC calendar (no alarm/wake)",
    .category = SOLAR_OS_EXPANSION_CATEGORY_SENSOR,
    .required_capabilities = SOLAR_OS_BOARD_CAP_I2C,
    .probe_supported = true, .binding_specs = specs, .binding_spec_count = 2,
    .attach = attach, .detach = detach,
};
