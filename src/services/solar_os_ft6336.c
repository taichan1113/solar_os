#include "solar_os_ft6336.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_check.h"
#include "ft6336.h"
#include "esp_timer.h"
#include "solar_os_cdc.h"
#include "solar_os_memory.h"
#include "solar_os_ft6336_raw.h"
#if defined(SOLAR_OS_SKETCH_LATENCY_TRIAL) && SOLAR_OS_SKETCH_LATENCY_TRIAL
#include "solar_os_sketch_trace.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "solar_os_display.h"
#include "solar_os_input.h"
#include "solar_os_task.h"
#include "solar_os_gpio_controller.h"
#include "esp_log.h"

#define FT6336_POLL_MS 10U
#define FT6336_TASK_STACK 3072U
#define FT6336_TASK_PRIORITY (tskIDLE_PRIORITY + 1)

typedef struct {
    bool active;
    bool ready;
    bool requested;
    bool suspended;
    bool managed_power;
    bool off_complete;
    bool pressed;
    bool frame_mode;
    volatile bool stop_requested;
    volatile bool worker_done;
    char name[SOLAR_OS_EXPANSION_DEVICE_NAME_MAX];
    char i2c_bus[SOLAR_OS_EXPANSION_TARGET_MAX];
    uint8_t address;
    int reset_pin;
    int irq_pin;
    bool external_reset;
    bool power_control;
    solar_os_gpio_line_ref_t reset_line;
    solar_os_gpio_line_ref_t power_line;
    uint8_t rotation;
    uint16_t target_width;
    uint16_t target_height;
    uint8_t pointer_id;
    int16_t x;
    int16_t y;
    solar_os_input_source_t input_source;
    TaskHandle_t worker_task;
} solar_os_ft6336_device_t;

static const char *TAG = "ft6336";
static solar_os_ft6336_device_t touch;

static void ft6336_worker(void *arg);

#include "solar_os_ft6336_raw.inc"

static esp_err_t parse_bindings(const solar_os_expansion_binding_t *bindings,
                                size_t binding_count,
                                solar_os_ft6336_device_t *device)
{
    bool have_i2c = false;
    bool have_address = false;
    bool have_reset = false;
    bool have_irq = false;
    bool have_rotation = false;
    bool have_enabled = false, have_managed = false;

    if (bindings == NULL || device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    device->reset_pin = -1;
    device->irq_pin = -1;
    device->requested = true; /* Legacy boards keep touch enabled. */

    for (size_t i = 0; i < binding_count; i++) {
        const solar_os_expansion_binding_t *binding = &bindings[i];
        switch (binding->kind) {
        case SOLAR_OS_EXPANSION_BINDING_I2C_BUS:
            if (have_i2c) {
                return ESP_ERR_INVALID_ARG;
            }
            strlcpy(device->i2c_bus,
                    binding->target,
                    sizeof(device->i2c_bus));
            have_i2c = true;
            break;
        case SOLAR_OS_EXPANSION_BINDING_I2C_ADDRESS:
            if (have_address || binding->value != FT6336_ADDRESS) {
                return ESP_ERR_INVALID_ARG;
            }
            device->address = (uint8_t)binding->value;
            have_address = true;
            break;
        case SOLAR_OS_EXPANSION_BINDING_GPIO:
            if (strcmp(binding->role, "reset") == 0 && !have_reset) {
                device->reset_pin = binding->value;
                have_reset = true;
            } else if (strcmp(binding->role, "irq") == 0 && !have_irq) {
                device->irq_pin = binding->value;
                have_irq = true;
            } else {
                return ESP_ERR_INVALID_ARG;
            }
            break;
        case SOLAR_OS_EXPANSION_BINDING_PARAMETER:
            if (strcmp(binding->role, "enabled") == 0 && !have_enabled &&
                (binding->value == 0 || binding->value == 1)) {
                device->requested = binding->value != 0; have_enabled = true;
            } else if (strcmp(binding->role, "managed_power") == 0 && !have_managed &&
                (binding->value == 0 || binding->value == 1)) {
                device->managed_power = binding->value != 0; have_managed = true;
            } else if (strcmp(binding->role, "rotation") == 0 && !have_rotation &&
                       binding->value >= 0 && binding->value <= 3) {
                device->rotation = (uint8_t)binding->value; have_rotation = true;
            } else {
                return ESP_ERR_INVALID_ARG;
            }
            break;
        case SOLAR_OS_EXPANSION_BINDING_GPIO_LINE: {
            solar_os_gpio_line_ref_t *line;
            if (strcmp(binding->role, "reset") == 0 && !have_reset) {
                device->external_reset = have_reset = true;
                line = &device->reset_line;
            } else if (strcmp(binding->role, "power") == 0 && !device->power_control) {
                device->power_control = true;
                line = &device->power_line;
            } else return ESP_ERR_INVALID_ARG;
            strlcpy(line->controller, binding->target, sizeof(line->controller));
            line->line = (uint8_t)binding->value;
            break;
        }
        default:
            return ESP_ERR_INVALID_ARG;
        }
    }

    return have_i2c && have_address && have_reset && have_irq && have_rotation &&
        (!device->managed_power || (device->power_control && device->external_reset))
        ? ESP_OK
        : ESP_ERR_INVALID_ARG;
}

static void clear_device(void)
{
    if (touch.input_source != SOLAR_OS_INPUT_SOURCE_INVALID) {
        solar_os_input_source_close(touch.input_source);
    }
    ft6336_deinit();
    if (touch.power_control) (void)solar_os_gpio_line_write(&touch.power_line, false);
    memset(&touch, 0, sizeof(touch));
}

static esp_err_t external_reset(void *context, bool high)
{
    const esp_err_t err = solar_os_gpio_line_write(context, high);
    /* PaperMono official sequence uses >=20ms reset LOW; the low-level
     * driver's existing 10ms delay supplies the other half. */
    if (err == ESP_OK && !high && touch.managed_power) vTaskDelay(pdMS_TO_TICKS(10));
    return err;
}

static esp_err_t stop_device(void)
{
    if (touch.off_complete && !touch.worker_task) return ESP_OK;
    touch.ready = false;
    (void)solar_os_input_source_set_ready(touch.input_source, false);
    if (touch.worker_task) {
        touch.stop_requested = true;
        if (!touch.worker_done) (void)xTaskNotifyGive(touch.worker_task);
        if (!solar_os_task_wait_done(touch.worker_task, &touch.worker_done,
                                     SOLAR_OS_TASK_STOP_WAIT_MS)) return ESP_ERR_TIMEOUT;
        touch.worker_task = NULL;
    }
    raw_finish(TOUCHRAW_END_SOURCE);
    esp_err_t err = solar_os_input_pointer_source_quiesce(touch.input_source);
    touch.pressed = false;
    touch.frame_mode = false;
    ft6336_deinit();
    if (touch.managed_power) {
        const esp_err_t irq = ft6336_disable_irq(touch.irq_pin);
        if (err == ESP_OK) err = irq;
    }
    if (touch.external_reset && touch.power_control) {
        const esp_err_t reset = solar_os_gpio_line_write(&touch.reset_line, false);
        if (err == ESP_OK) err = reset;
    }
    if (touch.power_control) {
        const esp_err_t power_err = solar_os_gpio_line_write(&touch.power_line, false);
        if (err == ESP_OK) err = power_err;
    }
    touch.off_complete = err == ESP_OK;
    if (err == ESP_OK) ESP_LOGI(TAG, "%s disabled: worker stopped, power OFF", touch.name);
    return err;
}

static esp_err_t start_device(void)
{
    if (touch.ready) return ESP_OK;
    /* Retry must never race a worker which timed out during shutdown. */
    esp_err_t err = stop_device();
    if (err != ESP_OK) return err;
    touch.off_complete = false;
    if (touch.power_control) {
        err = solar_os_gpio_line_write(&touch.power_line, true);
        if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (err == ESP_OK) err = touch.external_reset ?
        ft6336_init_with_reset(touch.i2c_bus, touch.address, touch.irq_pin,
                               external_reset, &touch.reset_line) :
        ft6336_init(touch.i2c_bus, touch.address, touch.reset_pin, touch.irq_pin);
    solar_os_touchraw_status_t diagnostic;
    solar_os_ft6336_raw_status(&diagnostic);
    if (err == ESP_OK) err = solar_os_input_source_set_ready(touch.input_source,
        diagnostic.mode != TOUCHRAW_WAIT_RELEASE);
    if (err == ESP_OK) {
        touch.stop_requested = touch.worker_done = false;
        touch.ready = true;
        if (solar_os_task_create_pinned_internal(ft6336_worker, touch.name,
            FT6336_TASK_STACK, &touch, FT6336_TASK_PRIORITY, &touch.worker_task,
            tskNO_AFFINITY, SOLAR_OS_TASK_ROLE_BACKGROUND) != pdPASS) {
            touch.worker_task = NULL;
            err = ESP_ERR_NO_MEM;
        }
    }
    if (err != ESP_OK) {
        const esp_err_t cleanup = stop_device();
        if (cleanup != ESP_OK) ESP_LOGE(TAG, "enable cleanup failed: %s", esp_err_to_name(cleanup));
        return err;
    }
    ESP_LOGI(TAG, "%s ready on %s 0x%02x", touch.name, touch.i2c_bus, touch.address);
    return ESP_OK;
}

bool solar_os_ft6336_is_ready(const char *name)
{
    return name && touch.active && !strcmp(name, touch.name) && touch.ready;
}
bool solar_os_ft6336_is_enabled(const char *name)
{
    return name && touch.active && !strcmp(name, touch.name) && touch.requested;
}
esp_err_t solar_os_ft6336_set_enabled(const char *name, bool enabled)
{
    if (!touch.active || !name || strcmp(name, touch.name)) return ESP_ERR_NOT_FOUND;
    touch.requested = enabled;
    return touch.requested && !touch.suspended ? start_device() : stop_device();
}
esp_err_t solar_os_ft6336_set_suspended(const char *name, bool suspended)
{
    if (!touch.active || !name || strcmp(name, touch.name)) return ESP_ERR_NOT_FOUND;
    if (!touch.managed_power) return ESP_OK;
    touch.suspended = suspended;
    return touch.requested && !touch.suspended ? start_device() : stop_device();
}

esp_err_t solar_os_ft6336_attach(const char *name,
                                 const solar_os_expansion_binding_t *bindings,
                                 size_t binding_count)
{
    if (name == NULL || name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (touch.active) {
        return ESP_ERR_INVALID_STATE;
    }

    solar_os_ft6336_device_t candidate = {0};
    ESP_RETURN_ON_ERROR(parse_bindings(bindings, binding_count, &candidate),
                        TAG,
                        "invalid bindings");
    solar_os_display_target_t target;
    if (!solar_os_display_find_target(SOLAR_OS_DISPLAY_PRIMARY_TARGET, &target) ||
        target.width == 0 || target.height == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    candidate.target_width = target.width;
    candidate.target_height = target.height;
    strlcpy(candidate.name, name, sizeof(candidate.name));
    esp_err_t err = solar_os_input_touch_source_open(candidate.name,
                                                     &candidate.input_source);
    if (err != ESP_OK) {
        ft6336_deinit();
        if (candidate.power_control) (void)solar_os_gpio_line_write(&candidate.power_line, false);
        return err;
    }
    candidate.active = true;
    touch = candidate;
    (void)solar_os_input_source_set_ready(touch.input_source, false);
    err = touch.requested ? start_device() : stop_device();
    if (err != ESP_OK) {
        clear_device();
        return err;
    }
    ESP_LOGI(TAG, "%s %s on %s 0x%02x: target=%ux%u rotation=%u",
             name, touch.ready ? "ready" : "disabled", touch.i2c_bus, touch.address,
             (unsigned)touch.target_width, (unsigned)touch.target_height,
             (unsigned)touch.rotation);
    return ESP_OK;
}

esp_err_t solar_os_ft6336_detach(const char *name)
{
    if (!touch.active || name == NULL || strcmp(touch.name, name) != 0) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t err = stop_device();
    if (err != ESP_OK) return err;
    clear_device();
    return ESP_OK;
}

static void poll_device(solar_os_ft6336_device_t *device)
{
    if (device == NULL || !device->ready || device->stop_requested) {
        return;
    }
    if (raw_poll(device)) return;

    solar_os_input_touch_ticket_t ticket;
    if (solar_os_input_touch_capture(device->input_source,
            SOLAR_OS_DISPLAY_PRIMARY_TARGET, &ticket) == ESP_OK) {
        if (!device->frame_mode) {
            /* A former legacy press must be released before complete frames
             * become the sole input path for this display. */
            if (solar_os_input_pointer_source_clear_legacy(device->input_source)
                != ESP_OK) return;
            device->pressed = false;
            device->frame_mode = true;
        }
        ft6336_frame_t frame = {0};
        const esp_err_t read_err = ft6336_read_frame(&frame);
        (void)solar_os_input_write_touch_frame(&ticket, ticket.next_sequence,
            (uint64_t)esp_timer_get_time(), read_err == ESP_OK ? &frame : NULL);
        return;
    }
    if (device->frame_mode) {
        device->frame_mode = false;
        device->pressed = false;
    }

    ft6336_sample_t sample;
#if defined(SOLAR_OS_SKETCH_LATENCY_TRIAL) && SOLAR_OS_SKETCH_LATENCY_TRIAL
    const int64_t read_start = esp_timer_get_time();
#endif
    const esp_err_t read_err = ft6336_read(&sample);
#if defined(SOLAR_OS_SKETCH_LATENCY_TRIAL) && SOLAR_OS_SKETCH_LATENCY_TRIAL
    const int32_t read_us = (int32_t)(esp_timer_get_time() - read_start);
    if (read_err != ESP_OK || sample.touched || device->pressed)
        solar_os_sketch_trace_record(SKTRACE_READ, 0, 0, 0,
            read_us, read_err);
#endif
    if (read_err != ESP_OK) {
        return;
    }

    uint16_t sample_x = 0;
    uint16_t sample_y = 0;
    if (sample.touched) {
        const uint16_t native_width = (device->rotation & 1U) != 0U
            ? device->target_height : device->target_width;
        const uint16_t native_height = (device->rotation & 1U) != 0U
            ? device->target_width : device->target_height;
        if (sample.x >= native_width || sample.y >= native_height) {
            return;
        }
        switch (device->rotation) {
        case 0:
            sample_x = sample.x;
            sample_y = sample.y;
            break;
        case 1:
            sample_x = sample.y;
            sample_y = (native_width - 1U) - sample.x;
            break;
        case 2:
            sample_x = (native_width - 1U) - sample.x;
            sample_y = (native_height - 1U) - sample.y;
            break;
        default:
            sample_x = (native_height - 1U) - sample.y;
            sample_y = sample.x;
            break;
        }
    }

    solar_os_input_pointer_action_t action;
    if (sample.touched && !device->pressed) {
        action = SOLAR_OS_INPUT_POINTER_PRESS;
    } else if (!sample.touched && device->pressed) {
        action = SOLAR_OS_INPUT_POINTER_RELEASE;
    } else if (sample.touched &&
               (sample_x != (uint16_t)device->x ||
                sample_y != (uint16_t)device->y)) {
        action = SOLAR_OS_INPUT_POINTER_MOVE;
    } else {
        return;
    }

    const int16_t next_x = sample.touched ? (int16_t)sample_x : device->x;
    const int16_t next_y = sample.touched ? (int16_t)sample_y : device->y;
    solar_os_input_pointer_event_t event = {
        .pointer_id = sample.touched ? sample.id : device->pointer_id,
        .buttons = sample.touched ? SOLAR_OS_INPUT_POINTER_BUTTON_PRIMARY : 0,
        .mode = SOLAR_OS_INPUT_POINTER_ABSOLUTE,
        .action = action,
        .x = next_x,
        .y = next_y,
        .delta_x = (int16_t)(next_x - device->x),
        .delta_y = (int16_t)(next_y - device->y),
    };
    strlcpy(event.target,
            SOLAR_OS_DISPLAY_PRIMARY_TARGET,
            sizeof(event.target));
#if defined(SOLAR_OS_SKETCH_LATENCY_TRIAL) && SOLAR_OS_SKETCH_LATENCY_TRIAL
    uint32_t stroke = 0;
    const uint32_t point = solar_os_sketch_trace_new_point(
        action == SOLAR_OS_INPUT_POINTER_PRESS, &stroke);
    solar_os_sketch_trace_record(SKTRACE_SAMPLE, point, stroke, 0,
        (int32_t)sample.x, (int32_t)sample.y);
    const esp_err_t push_err = solar_os_input_write_pointer_traced(
        device->input_source, &event, point);
#else
    const esp_err_t push_err = solar_os_input_write_pointer(device->input_source, &event);
#endif
    if (push_err == ESP_OK) {
        device->pressed = sample.touched;
        device->pointer_id = event.pointer_id;
        device->x = next_x;
        device->y = next_y;
    }
}

static void ft6336_worker(void *arg)
{
    solar_os_ft6336_device_t *device = arg;
    while (!device->stop_requested) {
        poll_device(device);
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(FT6336_POLL_MS));
    }
    raw_finish(TOUCHRAW_END_SOURCE);
    device->worker_done = true;
    solar_os_task_delete_internal(NULL);
}
