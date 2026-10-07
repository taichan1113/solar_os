#include "solar_os_ssd1677.h"

#include <stdbool.h>
#include <string.h>

#include "solar_os_axp2101.h"
#include "epd_ssd1677.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "solar_os_board_display.h"
#include "solar_os_buses.h"
#include "solar_os_display.h"
#include "solar_os_display_async.h"
#include "solar_os_gpio_controller.h"

#define SOLAR_OS_SSD1677_MAX 2
#define SOLAR_OS_SSD1677_DEFAULT_SPI_CLOCK_KHZ 20000

typedef struct {
    char spi_bus[SOLAR_OS_EXPANSION_TARGET_MAX];
    char power_i2c_bus[SOLAR_OS_EXPANSION_TARGET_MAX];
    int cs_pin;
    int dc_pin;
    int reset_pin;
    int busy_pin;
    int power_pin;
    int spi_clock_khz;
    int rotation;
    int power_i2c_address;
    bool external_reset;
    bool external_power;
    bool papermono_otp;
    solar_os_gpio_line_ref_t reset_line;
    solar_os_gpio_line_ref_t power_line;
} ssd1677_binding_config_t;

typedef struct {
    bool active;
    bool primary;
    char name[SOLAR_OS_EXPANSION_DEVICE_NAME_MAX];
    char spi_bus[SOLAR_OS_EXPANSION_TARGET_MAX];
    char power_i2c_bus[SOLAR_OS_EXPANSION_TARGET_MAX];
    int power_i2c_address;
    bool power_acquired;
    solar_os_gpio_line_ref_t reset_line;
    solar_os_gpio_line_ref_t power_line;
    epd_ssd1677_t driver;
    solar_os_display_async_t *executor;
    solar_os_board_display_t display;
} solar_os_ssd1677_device_t;

static const char *TAG = "ssd1677";
static solar_os_ssd1677_device_t *devices[SOLAR_OS_SSD1677_MAX];

static void stop_executor(solar_os_ssd1677_device_t *device)
{
    solar_os_display_async_destroy(device->executor);
    device->executor = NULL;
    device->driver.operation_begin = NULL;
    device->driver.operation_end = NULL;
    device->driver.execution_context = NULL;
}

static esp_err_t execute_frame(void *context, const uint8_t *frame, size_t size, bool *physical)
{
    solar_os_ssd1677_device_t *device = context;
    const int64_t previous = device->driver.timing.end_us;
    const esp_err_t err = epd_ssd1677_present_snapshot(&device->driver, frame, size);
    *physical = err == ESP_OK && device->driver.timing.end_us != previous;
    return err;
}

static esp_err_t execute_idle_cleanup(void *context, const uint8_t *frame, size_t size, bool *physical)
{
    solar_os_ssd1677_device_t *device = context;
    device->driver.cleanup_pending = true;
    ESP_LOGI(TAG, "typing idle cleanup: completed snapshot only");
    return execute_frame(context, frame, size, physical);
}

static void begin_operation(void *context, epd_ssd1677_operation_t op)
{
    solar_os_ssd1677_device_t *device = context;
    solar_os_display_async_control_begin(device->executor,
        op == EPD_SSD1677_OP_PRESENT || op == EPD_SSD1677_OP_SLEEP);
}

static void end_operation(void *context, epd_ssd1677_operation_t op, esp_err_t err)
{
    solar_os_ssd1677_device_t *device = context;
    if (op == EPD_SSD1677_OP_MODE && err == ESP_OK) {
        const bool typing = device->driver.refresh_mode == EPD_SSD1677_REFRESH_TYPING;
        solar_os_display_async_policy(device->executor,
            typing ? device->driver.typing_frame_ms : 0,
            typing ? device->driver.typing_idle_ms : 0,
            typing ? device->driver.typing_cleanup_budget : 0,
            typing ? execute_idle_cleanup : NULL);
    }
    int suspended = -1;
    if (err == ESP_OK && op == EPD_SSD1677_OP_SLEEP) suspended = 1;
    if (err == ESP_OK && op == EPD_SSD1677_OP_RESUME) suspended = 0;
    solar_os_display_async_control_end(device->executor, suspended,
        device->driver.faulted ? (device->driver.last_error != ESP_OK ?
        device->driver.last_error : ESP_FAIL) : ESP_OK);
}

static esp_err_t submit_frame(void *context, const uint8_t *frame, size_t size, uint64_t *id)
{
    return solar_os_display_async_submit(((solar_os_ssd1677_device_t *)context)->executor, frame, size, id);
}

static esp_err_t wait_frame(void *context, uint32_t timeout_ms)
{
    return solar_os_display_async_wait(((solar_os_ssd1677_device_t *)context)->executor, timeout_ms);
}

static esp_err_t frame_status(void *context, solar_os_display_submission_status_t *status)
{
    solar_os_display_async_status(((solar_os_ssd1677_device_t *)context)->executor, status);
    return ESP_OK;
}

static bool role_is(const solar_os_expansion_binding_t *binding, const char *role)
{
    return binding != NULL && role != NULL && strcmp(binding->role, role) == 0;
}

static int find_device_slot(const char *name)
{
    if (name == NULL) return -1;
    for (size_t i = 0; i < SOLAR_OS_SSD1677_MAX; i++) {
        if (devices[i] != NULL && devices[i]->active &&
            strcmp(devices[i]->name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int alloc_device_slot(void)
{
    for (size_t i = 0; i < SOLAR_OS_SSD1677_MAX; i++) {
        if (devices[i] == NULL) {
            devices[i] = heap_caps_calloc(1,
                                          sizeof(*devices[i]),
                                          MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            return devices[i] != NULL ? (int)i : -1;
        }
    }
    return -1;
}

static void release_device_slot(size_t slot)
{
    if (slot >= SOLAR_OS_SSD1677_MAX || devices[slot] == NULL) return;
    stop_executor(devices[slot]);
    epd_ssd1677_deinit(&devices[slot]->driver);
    if (devices[slot]->power_acquired) solar_os_axp2101_release_aldo3();
    heap_caps_free(devices[slot]);
    devices[slot] = NULL;
}

static const u8g2_cb_t *rotation_callback(int rotation)
{
    switch (rotation) {
    case 0: return U8G2_R0;
    case 1: return U8G2_R1;
    case 2: return U8G2_R2;
    case 3: return U8G2_R3;
    default: return NULL;
    }
}

static esp_err_t axp2101_set_epaper_power(void *context, bool on)
{
    solar_os_ssd1677_device_t *device = context;
    if (device == NULL || !device->power_acquired) {
        return ESP_ERR_INVALID_STATE;
    }
    return solar_os_axp2101_set_aldo3(on);
}

static esp_err_t gpio_set_epaper_power(void *context, bool on)
{
    solar_os_ssd1677_device_t *device = context;
    return solar_os_gpio_line_write(&device->power_line, on);
}

static esp_err_t gpio_set_epaper_reset(void *context, bool high)
{
    solar_os_ssd1677_device_t *device = context;
    return solar_os_gpio_line_write(&device->reset_line, high);
}

static esp_err_t parse_bindings(const solar_os_expansion_binding_t *bindings,
                                size_t binding_count,
                                ssd1677_binding_config_t *config)
{
    bool have_spi = false;
    bool have_cs = false;
    bool have_power_i2c = false;
    bool have_power_address = false;
    if (bindings == NULL || config == NULL) return ESP_ERR_INVALID_ARG;
    *config = (ssd1677_binding_config_t) {
        .cs_pin = -1,
        .dc_pin = -1,
        .reset_pin = -1,
        .busy_pin = -1,
        .power_pin = -1,
        .spi_clock_khz = SOLAR_OS_SSD1677_DEFAULT_SPI_CLOCK_KHZ,
        .rotation = 0,
        .power_i2c_address = -1,
    };
    for (size_t i = 0; i < binding_count; i++) {
        const solar_os_expansion_binding_t *binding = &bindings[i];
        switch (binding->kind) {
        case SOLAR_OS_EXPANSION_BINDING_SPI_BUS:
            if (have_spi) return ESP_ERR_INVALID_ARG;
            strlcpy(config->spi_bus, binding->target, sizeof(config->spi_bus));
            have_spi = true;
            break;
        case SOLAR_OS_EXPANSION_BINDING_SPI_CS:
            if (have_cs) return ESP_ERR_INVALID_ARG;
            config->cs_pin = binding->value;
            have_cs = true;
            if (binding->target[0] != '\0') {
                if (have_spi && strcmp(config->spi_bus, binding->target) != 0) {
                    return ESP_ERR_INVALID_ARG;
                }
                strlcpy(config->spi_bus, binding->target, sizeof(config->spi_bus));
                have_spi = true;
            }
            break;
        case SOLAR_OS_EXPANSION_BINDING_I2C_BUS:
            if (!role_is(binding, "power") || have_power_i2c) {
                return ESP_ERR_INVALID_ARG;
            }
            strlcpy(config->power_i2c_bus,
                    binding->target,
                    sizeof(config->power_i2c_bus));
            have_power_i2c = true;
            break;
        case SOLAR_OS_EXPANSION_BINDING_GPIO:
            if (role_is(binding, "dc") && config->dc_pin < 0) {
                config->dc_pin = binding->value;
            } else if ((role_is(binding, "reset") || role_is(binding, "rst")) &&
                       config->reset_pin < 0) {
                config->reset_pin = binding->value;
            } else if (role_is(binding, "busy") && config->busy_pin < 0) {
                config->busy_pin = binding->value;
            } else if (role_is(binding, "power") && config->power_pin < 0) {
                config->power_pin = binding->value;
            } else {
                return ESP_ERR_INVALID_ARG;
            }
            break;
        case SOLAR_OS_EXPANSION_BINDING_GPIO_LINE: {
            solar_os_gpio_line_ref_t *line = NULL;
            if (role_is(binding, "reset") && !config->external_reset && config->reset_pin < 0) {
                config->external_reset = true;
                line = &config->reset_line;
            } else if (role_is(binding, "power") && !config->external_power && config->power_pin < 0) {
                config->external_power = true;
                line = &config->power_line;
            } else return ESP_ERR_INVALID_ARG;
            strlcpy(line->controller, binding->target, sizeof(line->controller));
            line->line = (uint8_t)binding->value;
            break;
        }
        case SOLAR_OS_EXPANSION_BINDING_PARAMETER:
            if (role_is(binding, "clock")) {
                config->spi_clock_khz = binding->value;
            } else if (role_is(binding, "rotation")) {
                config->rotation = binding->value;
            } else if (role_is(binding, "panel") && (binding->value == 0 || binding->value == 1)) {
                config->papermono_otp = binding->value == 1;
            } else if (role_is(binding, "power_addr") && !have_power_address) {
                config->power_i2c_address = binding->value;
                have_power_address = true;
            } else {
                return ESP_ERR_INVALID_ARG;
            }
            break;
        default:
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (!have_spi || !have_cs || config->dc_pin < 0 ||
        (!config->external_reset && config->reset_pin < 0) ||
        config->busy_pin < 0 || config->spi_clock_khz < 100 ||
        config->spi_clock_khz > 20000 || rotation_callback(config->rotation) == NULL ||
        config->cs_pin == config->dc_pin || config->cs_pin == config->reset_pin ||
        config->cs_pin == config->busy_pin || config->dc_pin == config->reset_pin ||
        config->dc_pin == config->busy_pin || config->reset_pin == config->busy_pin ||
        have_power_i2c != have_power_address ||
        (have_power_i2c &&
         (config->power_i2c_address != 0x34 || config->power_pin >= 0 || config->external_power ||
          !solar_os_expansion_find_i2c_bus(config->power_i2c_bus, NULL, NULL))) ||
        (config->power_pin >= 0 &&
         (config->power_pin == config->cs_pin || config->power_pin == config->dc_pin ||
          config->power_pin == config->reset_pin || config->power_pin == config->busy_pin)) ||
        !solar_os_expansion_find_spi_bus(config->spi_bus, NULL, NULL) ||
        !solar_os_expansion_spi_cs_allowed(config->spi_bus, config->cs_pin)) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t display_runtime_ready(solar_os_board_display_t *display)
{
    return display != NULL && display->driver != NULL ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static esp_err_t display_resume(solar_os_board_display_t *display)
{
    const esp_err_t ret = display != NULL && display->driver != NULL ?
        epd_ssd1677_resume(display->driver) : ESP_ERR_INVALID_STATE;
    if (display != NULL) display->ready = ret == ESP_OK;
    return ret;
}

static void display_deinit(solar_os_board_display_t *display)
{
    if (display != NULL && display->driver != NULL) {
        epd_ssd1677_t *driver = display->driver;
        if (driver->execution_context) stop_executor(driver->execution_context);
        epd_ssd1677_deinit(display->driver);
        display->ready = false;
    }
}

static esp_err_t display_power_save(solar_os_board_display_t *display, bool enabled)
{
    return display ? epd_ssd1677_set_power_save(display->driver, enabled) : ESP_ERR_INVALID_ARG;
}

static esp_err_t display_set_colors(solar_os_board_display_t *display,
                                    uint32_t foreground,
                                    uint32_t background)
{
    (void)display;
    (void)foreground;
    (void)background;
    return ESP_OK;
}

static const char *display_controller_mode(const solar_os_board_display_t *display)
{
    return display != NULL ? epd_ssd1677_controller_mode(display->driver) : NULL;
}

static const char *display_controller_mode_values(const solar_os_board_display_t *display)
{
    return display != NULL ? epd_ssd1677_controller_mode_values(display->driver) : NULL;
}

static esp_err_t display_set_controller_mode(solar_os_board_display_t *display,
                                             const char *mode)
{
    if (display && display->driver && mode && strcmp(mode, "refresh=typing") == 0 &&
        !((epd_ssd1677_t *)display->driver)->execution_context) return ESP_ERR_NOT_SUPPORTED;
    return display != NULL ? epd_ssd1677_set_controller_mode(display->driver, mode) :
        ESP_ERR_INVALID_STATE;
}

static const solar_os_board_display_ops_t display_ops = {
    .runtime_ready = display_runtime_ready,
    .resume = display_resume,
    .deinit = display_deinit,
    .set_power_save = display_power_save,
    .set_colors = display_set_colors,
    .controller_mode = display_controller_mode,
    .controller_mode_values = display_controller_mode_values,
    .set_controller_mode = display_set_controller_mode,
};

static const char *target_controller_mode(const void *context)
{
    return epd_ssd1677_controller_mode(context);
}

static const char *target_controller_mode_values(const void *context)
{
    return epd_ssd1677_controller_mode_values(context);
}

static esp_err_t target_set_controller_mode(void *context, const char *mode)
{
    if (context && mode && strcmp(mode, "refresh=typing") == 0 &&
        !((epd_ssd1677_t *)context)->execution_context) return ESP_ERR_NOT_SUPPORTED;
    return epd_ssd1677_set_controller_mode(context, mode);
}

static esp_err_t register_auxiliary(solar_os_ssd1677_device_t *device)
{
    solar_os_display_target_t target = {0};
    strlcpy(target.name, device->name, sizeof(target.name));
    strlcpy(target.source, "expansion", sizeof(target.source));
    strlcpy(target.driver, "ssd1677", sizeof(target.driver));
    strlcpy(target.controller, "SSD1677", sizeof(target.controller));
    strlcpy(target.role, "aux", sizeof(target.role));
    target.width = u8g2_GetDisplayWidth(device->display.u8g2);
    target.height = u8g2_GetDisplayHeight(device->display.u8g2);
    target.ready = true;
    target.u8g2 = device->display.u8g2;
    target.controller_context = &device->driver;
    target.controller_mode = target_controller_mode;
    target.controller_mode_values = target_controller_mode_values;
    target.set_controller_mode = target_set_controller_mode;
    target.submission = device->display.submission;
    return solar_os_display_register_target(&target);
}

esp_err_t solar_os_ssd1677_attach(const char *name,
                                  const solar_os_expansion_binding_t *bindings,
                                  size_t binding_count)
{
    if (name == NULL || name[0] == '\0' ||
        strnlen(name, SOLAR_OS_DISPLAY_TARGET_NAME_MAX) >= SOLAR_OS_DISPLAY_TARGET_NAME_MAX ||
        find_device_slot(name) >= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    ssd1677_binding_config_t binding_config;
    ESP_RETURN_ON_ERROR(parse_bindings(bindings, binding_count, &binding_config),
                        TAG, "invalid bindings");
    const int slot = alloc_device_slot();
    if (slot < 0) return ESP_ERR_NO_MEM;
    solar_os_ssd1677_device_t *device = devices[slot];
    strlcpy(device->name, name, sizeof(device->name));
    strlcpy(device->spi_bus, binding_config.spi_bus, sizeof(device->spi_bus));
    strlcpy(device->power_i2c_bus,
            binding_config.power_i2c_bus,
            sizeof(device->power_i2c_bus));
    device->power_i2c_address = binding_config.power_i2c_address;
    device->reset_line = binding_config.reset_line;
    device->power_line = binding_config.power_line;
    if (binding_config.power_i2c_address >= 0) {
        const esp_err_t power_ret = solar_os_axp2101_acquire_aldo3(
            device->power_i2c_bus, (uint8_t)device->power_i2c_address);
        if (power_ret != ESP_OK) {
            heap_caps_free(device);
            devices[slot] = NULL;
            return power_ret;
        }
        device->power_acquired = true;
    }
    const epd_ssd1677_config_t config = {
        .spi_bus = device->spi_bus,
        .cs_pin = binding_config.cs_pin,
        .dc_pin = binding_config.dc_pin,
        .reset_pin = binding_config.reset_pin,
        .busy_pin = binding_config.busy_pin,
        .power_pin = binding_config.power_pin,
        .spi_clock_hz = binding_config.spi_clock_khz * 1000,
        .busy_level = 1,
        .power_active_level = 1,
        .rotation = rotation_callback(binding_config.rotation),
        .set_power = binding_config.power_i2c_address >= 0 ?
            axp2101_set_epaper_power : (binding_config.external_power ? gpio_set_epaper_power : NULL),
        .power_context = device,
        .set_reset = binding_config.external_reset ? gpio_set_epaper_reset : NULL,
        .reset_context = device,
        .papermono_otp = binding_config.papermono_otp,
    };
    esp_err_t ret = epd_ssd1677_init(&device->driver, &config);
    if (ret != ESP_OK) {
        release_device_slot((size_t)slot);
        return ret;
    }
    device->active = true;
    device->primary = solar_os_display_target_is_board_primary(name);
    u8g2_t *const u8g2 = epd_ssd1677_get_u8g2(&device->driver);
    device->display = (solar_os_board_display_t) {
        .ops = &display_ops,
        .driver = &device->driver,
        .driver_name = "ssd1677",
        .u8g2 = u8g2,
        .controller = "SSD1677",
        .width = u8g2_GetDisplayWidth(u8g2),
        .height = u8g2_GetDisplayHeight(u8g2),
        .ready = true,
    };
    if (binding_config.papermono_otp) {
        const size_t internal_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        const size_t psram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        const esp_err_t async_err = solar_os_display_async_create(device->driver.buffer_size,
            execute_frame, device, &device->executor);
        if (async_err == ESP_OK) {
            device->driver.execution_context = device;
            device->driver.operation_begin = begin_operation;
            device->driver.operation_end = end_operation;
            device->display.submission = (solar_os_display_submission_t) {
                .context = device, .submit = submit_frame, .wait = wait_frame, .status = frame_status,
            };
            ESP_LOGI(TAG, "async snapshots=%u internal=%u->%u psram=%u->%u minimum=%u/%u",
                (unsigned)(device->driver.buffer_size * 2), (unsigned)internal_before,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                (unsigned)psram_before, (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
        } else ESP_LOGW(TAG, "async unavailable (%d); synchronous fallback", async_err);
    }
    ret = device->primary ?
        solar_os_board_display_register_primary(&device->display) :
        register_auxiliary(device);
    if (ret != ESP_OK) {
        release_device_slot((size_t)slot);
        return ret;
    }
    ESP_LOGI(TAG,
             "%s attached on %s CS GPIO%d DC GPIO%d RST GPIO%d BUSY GPIO%d",
             name, device->spi_bus, binding_config.cs_pin, binding_config.dc_pin,
             binding_config.reset_pin, binding_config.busy_pin);
    return ESP_OK;
}

esp_err_t solar_os_ssd1677_detach(const char *name)
{
    const int slot = find_device_slot(name);
    if (slot < 0) return ESP_ERR_NOT_FOUND;
    solar_os_ssd1677_device_t *device = devices[slot];
    if (device->primary) {
        solar_os_display_target_t target;
        if (solar_os_display_find_target(name, &target)) {
            const esp_err_t unregister_err = solar_os_display_unregister_target(name);
            if (unregister_err != ESP_OK) return unregister_err;
        }
    }
    const esp_err_t ret = device->primary ?
        solar_os_board_display_unregister_primary(&device->display) :
        solar_os_display_unregister_target(name);
    if (ret != ESP_OK) return ret;
    release_device_slot((size_t)slot);
    return ESP_OK;
}
