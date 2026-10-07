/* PaperMono power adapter. Register definitions/sequencing from M5Stack's
 * M5GFX, M5IOE1 and M5PM1 (MIT); see doc/m5paper-mono.md for pinned sources.
 * IOE lines are zero-based: PYG1=0 ... PYG14=13.
 */
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "solar_os_buses.h"
#include "solar_os_expansion.h"
#include "solar_os_gpio_controller.h"
#include "solar_os_battery.h"
#include "solar_os_status_led.h"
#include "esp_timer.h"

static const char *TAG = "papermono-power";
static struct {
    bool active;
    char bus[SOLAR_OS_EXPANSION_TARGET_MAX];
    char name[SOLAR_OS_GPIO_CONTROLLER_NAME_MAX];
    SemaphoreHandle_t mutex;
    StaticSemaphore_t mutex_storage;
    bool sample_valid;
    uint32_t sample_ms;
    esp_err_t sample_error;
    solar_os_battery_sample_t sample;
} power;

static esp_err_t update_reg(uint8_t addr, uint8_t reg, uint8_t mask, uint8_t bits)
{
    uint8_t value;
    ESP_RETURN_ON_ERROR(solar_os_bus_i2c_read_reg(power.bus, addr, reg, &value, 1),
                        TAG, "register read failed");
    value = (value & ~mask) | (bits & mask);
    return solar_os_bus_i2c_write_reg(power.bus, addr, reg, &value, 1);
}

static esp_err_t configure(void *ctx, uint8_t line, solar_os_gpio_line_mode_t mode,
                           solar_os_gpio_line_pull_t pull)
{
    (void)ctx;
    if (!power.active || line >= 14) return ESP_ERR_INVALID_ARG;
    if (line == 10) return ESP_ERR_NOT_SUPPORTED; /* Reserved charger bus mux. */
    const uint8_t bank = line / 8, mask = 1U << (line % 8);
    xSemaphoreTake(power.mutex, portMAX_DELAY);
    esp_err_t err = update_reg(0x4f, 0x09 + bank, mask,
                               pull == SOLAR_OS_GPIO_LINE_PULL_UP ? mask : 0);
    if (err == ESP_OK) err = update_reg(0x4f, 0x0b + bank, mask,
                                       pull == SOLAR_OS_GPIO_LINE_PULL_DOWN ? mask : 0);
    if (err == ESP_OK) err = update_reg(0x4f, 0x13 + bank, mask, 0); /* push-pull */
    if (err == ESP_OK) err = update_reg(0x4f, 0x03 + bank, mask,
                                       mode == SOLAR_OS_GPIO_LINE_MODE_OUTPUT ? mask : 0);
    xSemaphoreGive(power.mutex);
    return err;
}

static esp_err_t read_line(void *ctx, uint8_t line, bool *level)
{
    (void)ctx;
    if (!power.active || line >= 14 || level == NULL) return ESP_ERR_INVALID_ARG;
    uint8_t value;
    xSemaphoreTake(power.mutex, portMAX_DELAY);
    esp_err_t err = solar_os_bus_i2c_read_reg(power.bus, 0x4f, 0x07 + line / 8, &value, 1);
    xSemaphoreGive(power.mutex);
    if (err == ESP_OK) *level = (value & (1U << (line % 8))) != 0;
    return err;
}

static esp_err_t write_line(void *ctx, uint8_t line, bool level)
{
    (void)ctx;
    if (!power.active || line >= 14) return ESP_ERR_INVALID_ARG;
    if (line == 10) return ESP_ERR_NOT_SUPPORTED;
    const uint8_t bank = line / 8, mask = 1U << (line % 8);
    xSemaphoreTake(power.mutex, portMAX_DELAY);
    /* Preload before driving, preserve every unrelated line. */
    esp_err_t err = update_reg(0x4f, 0x05 + bank, mask, level ? mask : 0);
    if (err == ESP_OK) err = update_reg(0x4f, 0x13 + bank, mask, 0);
    if (err == ESP_OK) err = update_reg(0x4f, 0x03 + bank, mask, mask);
    xSemaphoreGive(power.mutex);
    if (err == ESP_OK && (line == 2 || line == 4 || line == 5 || line == 12)) {
        ESP_LOGI(TAG, "IOE PYG%u=%u", (unsigned)line + 1, (unsigned)level);
    }
    return err;
}

static const solar_os_gpio_controller_ops_t ops = {
    .configure = configure, .read = read_line, .write = write_line,
};

static esp_err_t led_set(void *ctx, bool on)
{
    (void)ctx;
    if (!power.active) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(power.mutex, portMAX_DELAY);
    const esp_err_t err = update_reg(0x6e, 0x06, 0x10, on ? 0x10 : 0);
    xSemaphoreGive(power.mutex);
    return err;
}
static esp_err_t led_get(void *ctx, bool *on)
{
    (void)ctx;
    if (!power.active || !on) return ESP_ERR_INVALID_STATE;
    uint8_t reg;
    xSemaphoreTake(power.mutex, portMAX_DELAY);
    const esp_err_t err = solar_os_bus_i2c_read_reg(power.bus, 0x6e, 0x06, &reg, 1);
    xSemaphoreGive(power.mutex);
    if (err == ESP_OK) *on = (reg & 0x10) != 0;
    return err;
}

static esp_err_t battery_session(const solar_os_i2c_transaction_t *s, void *user)
{
    solar_os_battery_sample_t *sample = user;
    uint8_t voltage[2], source, config;
    esp_err_t err = s->read_reg(s->context, 0x6e, 0x22, voltage, 2);
    if (err == ESP_OK) err = s->read_reg(s->context, 0x6e, 0x04, &source, 1);
    if (err == ESP_OK) err = s->read_reg(s->context, 0x6e, 0x06, &config, 1);
    if (err != ESP_OK) return err;
    /* M5PM1 datasheet specifies two 8-bit little-endian millivolt registers.
     * Power source is a bitmap, not an enum. No SOC/battery presence inference. */
    sample->battery_mv = voltage[0] | ((uint16_t)voltage[1] << 8);
    if (sample->battery_mv == 0) return ESP_ERR_INVALID_STATE;
    sample->percent_unavailable = true;
    sample->external_power_valid = true;
    sample->external_power = (source & 0x03) != 0;
    if (!sample->external_power || !(config & 1)) {
        sample->charging_valid = true;
        return ESP_OK;
    }
    /* Enable the charger bus only within this exclusive I2C session. Never
     * write an IP2315 register. Final-byte NACK is supplied by the I2C backend. */
    uint8_t latch, enable = 0, status = 0;
    err = s->read_reg(s->context, 0x4f, 0x06, &latch, 1);
    if (err != ESP_OK) return err;
    const uint8_t selected = latch | 4, disconnected = latch & ~4;
    err = s->write_reg(s->context, 0x4f, 0x06, &selected, 1);
    if (err == ESP_OK) err = s->read_reg(s->context, 0x75, 0x01, &enable, 1);
    if (err == ESP_OK) err = s->read_reg(s->context, 0x75, 0xc7, &status, 1);
    /* Selection write may have reached hardware even when it reports failure. */
    const esp_err_t cleanup = s->write_reg(s->context, 0x4f, 0x06, &disconnected, 1);
    if (cleanup != ESP_OK) return cleanup;
    if (err == ESP_OK) {
        sample->charging_valid = true;
        sample->charging = (enable & 1) && !(status & 0x40);
        sample->charge_complete_valid = (enable & 1) != 0;
        sample->charge_complete = sample->charge_complete_valid && (status & 0x40);
    } else {
        /* USB insertion mode can make charger I2C unavailable at low VBAT.
         * Retain real PM1 voltage/source and explicitly unknown charge state. */
        ESP_LOGW(TAG, "charger status unavailable: %s (mux disconnected)", esp_err_to_name(err));
    }
    return ESP_OK;
}

static esp_err_t battery_read(void *ctx, solar_os_battery_sample_t *sample)
{
    (void)ctx;
    if (!power.active || !sample) return ESP_ERR_INVALID_STATE;
    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    xSemaphoreTake(power.mutex, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    if (!power.sample_valid || (uint32_t)(now - power.sample_ms) >= 30000U) {
        solar_os_battery_sample_t next = {0};
        err = solar_os_bus_i2c_transaction(power.bus, battery_session, &next);
        /* Cache errors too: never turn main-task status cadence into rapid
         * I2C retries. On failure do not serve a stale measurement as valid. */
        power.sample = next;
        power.sample_valid = true;
        power.sample_ms = now;
        power.sample_error = err;
        if (err != ESP_OK) power.sample.battery_mv = 0;
    }
    if (!power.sample.battery_mv) err = power.sample_error != ESP_OK ? power.sample_error : ESP_ERR_INVALID_STATE;
    if (err == ESP_OK) *sample = power.sample;
    xSemaphoreGive(power.mutex);
    return err;
}

static esp_err_t attach(const char *name, const solar_os_expansion_binding_t *bindings,
                        size_t count)
{
    if (!name || !name[0] || strlen(name) >= sizeof(power.name) || power.active)
        return ESP_ERR_INVALID_ARG;
    bool bus = false, ioe = false, pm = false;
    memset(&power, 0, sizeof(power));
    for (size_t i = 0; i < count; i++) {
        const solar_os_expansion_binding_t *b = &bindings[i];
        if (b->kind == SOLAR_OS_EXPANSION_BINDING_I2C_BUS && !bus) {
            strlcpy(power.bus, b->target, sizeof(power.bus)); bus = true;
        } else if (b->kind == SOLAR_OS_EXPANSION_BINDING_I2C_ADDRESS && b->value == 0x4f && !ioe) {
            ioe = true;
        } else if (b->kind == SOLAR_OS_EXPANSION_BINDING_I2C_ADDRESS && b->value == 0x6e && !pm) {
            pm = true;
        } else return ESP_ERR_INVALID_ARG;
    }
    if (!bus || !ioe || !pm) return ESP_ERR_INVALID_ARG;
    ESP_LOGI(TAG, "stage 1: M5PM1/M5IOE1 settling");
    vTaskDelay(pdMS_TO_TICKS(500));
    uint8_t id[2];
    esp_err_t err = ESP_FAIL;
    /* An initial transaction can wake a retained sleeping PMIC. Only retry
     * discovery; never retry panel power/refresh after a hardware error. */
    for (unsigned attempt = 0; attempt < 3; attempt++) {
        err = solar_os_bus_i2c_read_reg(power.bus, 0x6e, 0, id, sizeof(id));
        if (err == ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    ESP_RETURN_ON_ERROR(err, TAG, "M5PM1 discovery failed");
    if (id[0] != 0x50 || id[1] != 0x20) return ESP_ERR_NOT_SUPPORTED;
    ESP_RETURN_ON_ERROR(update_reg(0x6e, 0x09, 0xff, 0), TAG, "PM1 idle sleep disable failed");
    ESP_RETURN_ON_ERROR(update_reg(0x6e, 0x0a, 0xff, 0), TAG, "PM1 watchdog disable failed");
    ESP_RETURN_ON_ERROR(update_reg(0x6e, 0x06, 0x17, 0x07), TAG, "PM1 rails/LED OFF setup failed");
    ESP_LOGI(TAG, "stage 2: M5PM1 ready (ID 2050)");
    ESP_RETURN_ON_ERROR(solar_os_bus_i2c_read_reg(power.bus, 0x4f, 0, id, sizeof(id)),
                        TAG, "M5IOE1 discovery failed");
    ESP_RETURN_ON_ERROR(update_reg(0x4f, 0x23, 0xff, 0), TAG, "IOE idle sleep disable failed");
    /* Disconnect IP2315 charger I2C mux (PYG11). No charger programming. */
    ESP_RETURN_ON_ERROR(update_reg(0x4f, 0x20, 0x80, 0), TAG, "IOE PWM3 disable failed");
    ESP_RETURN_ON_ERROR(update_reg(0x4f, 0x06, 0x04, 0), TAG, "charger mux latch failed");
    ESP_RETURN_ON_ERROR(update_reg(0x4f, 0x14, 0x04, 0), TAG, "charger mux drive failed");
    ESP_RETURN_ON_ERROR(update_reg(0x4f, 0x04, 0x04, 0x04), TAG, "charger mux mode failed");
    /* IOE RGB: PWM1=PYG9 blue, PWM2=PYG8 green. Disable PWM ownership,
     * preload LOW, and select push-pull outputs without changing other pins. */
    ESP_RETURN_ON_ERROR(update_reg(0x4f, 0x1c, 0x80, 0), TAG, "blue PWM disable failed");
    ESP_RETURN_ON_ERROR(update_reg(0x4f, 0x1e, 0x80, 0), TAG, "green PWM disable failed");
    ESP_RETURN_ON_ERROR(update_reg(0x4f, 0x05, 0x80, 0), TAG, "green OFF failed");
    ESP_RETURN_ON_ERROR(update_reg(0x4f, 0x06, 0x01, 0), TAG, "blue OFF failed");
    ESP_RETURN_ON_ERROR(update_reg(0x4f, 0x13, 0x80, 0), TAG, "green drive failed");
    ESP_RETURN_ON_ERROR(update_reg(0x4f, 0x14, 0x01, 0), TAG, "blue drive failed");
    ESP_RETURN_ON_ERROR(update_reg(0x4f, 0x03, 0x80, 0x80), TAG, "green mode failed");
    ESP_RETURN_ON_ERROR(update_reg(0x4f, 0x04, 0x01, 0x01), TAG, "blue mode failed");
    power.mutex = xSemaphoreCreateMutexStatic(&power.mutex_storage);
    if (!power.mutex) return ESP_ERR_NO_MEM;
    strlcpy(power.name, name, sizeof(power.name));
    power.active = true;
    const solar_os_gpio_controller_registration_t registration = {
        .name = name, .line_count = 14, .ops = &ops, .ctx = &power,
    };
    err = solar_os_gpio_controller_register(&registration);
    if (err != ESP_OK) { power.active = false; return err; }
    const solar_os_status_led_provider_t led = {.set = led_set, .get = led_get};
    const solar_os_battery_provider_t battery = {.read = battery_read};
    err = solar_os_status_led_register_provider(name, &led);
    if (err == ESP_OK) {
        err = solar_os_battery_register_provider(name, &battery);
        if (err != ESP_OK) (void)solar_os_status_led_unregister_provider(name);
    }
    if (err != ESP_OK) {
        (void)solar_os_gpio_controller_unregister(name);
        power.active = false;
    } else ESP_LOGI(TAG, "stage 3: M5IOE1 ready (UID %02x%02x), controller=%s, RGB OFF", id[1], id[0], name);
    return err;
}

static esp_err_t detach(const char *name)
{
    if (!power.active || !name || strcmp(name, power.name)) return ESP_ERR_NOT_FOUND;
    ESP_RETURN_ON_ERROR(solar_os_gpio_controller_unregister(name), TAG, "GPIO lines in use");
    (void)solar_os_battery_unregister_provider(name);
    (void)solar_os_status_led_unregister_provider(name);
    power.active = false;
    return ESP_OK; /* Never shut down the MCU rail on detach. */
}

static const int ioe_addresses[] = {0x4f}, pm_addresses[] = {0x6e};
static const solar_os_expansion_binding_spec_t specs[] = {
    {.key = "i2c", .value_hint = "bus", .kind = SOLAR_OS_EXPANSION_BINDING_I2C_BUS, .required = true},
    {.key = "addr", .value_hint = "0x4f", .kind = SOLAR_OS_EXPANSION_BINDING_I2C_ADDRESS, .required = true,
     .allowed_values = ioe_addresses, .allowed_value_count = 1},
    {.key = "pm_addr", .value_hint = "0x6e", .role = "pm", .kind = SOLAR_OS_EXPANSION_BINDING_I2C_ADDRESS, .required = true,
     .allowed_values = pm_addresses, .allowed_value_count = 1},
};
const solar_os_expansion_driver_t solar_os_m5papermono_power_expansion_driver = {
    .name = "m5papermono-power", .summary = "PaperMono M5PM1 and M5IOE1 power/GPIO",
    .category = SOLAR_OS_EXPANSION_CATEGORY_UTILITY,
    .required_capabilities = SOLAR_OS_BOARD_CAP_EXPANSION_I2C,
    .early = true, .binding_specs = specs, .binding_spec_count = sizeof(specs) / sizeof(specs[0]),
    .attach = attach, .detach = detach,
};
