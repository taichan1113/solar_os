#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#include "solar_os_buttons.h"
#include "solar_os_input.h"
#include "solar_os_log.h"

static uint32_t now_ms;
static bool pressed;
static uint8_t emitted[8];
static size_t emitted_count;

int64_t esp_timer_get_time(void) { return (int64_t)now_ms * 1000; }
esp_err_t gpio_config(const gpio_config_t *config)
{
    assert(config->pin_bit_mask == (1ULL << 3));
    return ESP_OK;
}
int gpio_get_level(gpio_num_t pin)
{
    assert(pin == 3);
    return pressed ? 0 : 1;
}
esp_err_t solar_os_input_key_source_open(const char *name,
                                         solar_os_input_source_class_t source_class,
                                         solar_os_input_source_t *source)
{
    assert(source_class == SOLAR_OS_INPUT_SOURCE_BUTTONS);
    assert(name != NULL);
    *source = 1;
    return ESP_OK;
}
esp_err_t solar_os_input_write_char(solar_os_input_source_t source, char ch)
{
    assert(source == 1);
    assert(emitted_count < sizeof(emitted));
    emitted[emitted_count++] = (uint8_t)ch;
    return ESP_OK;
}
esp_err_t solar_os_input_write_key(solar_os_input_source_t source,
                                   uint16_t physical_key, uint16_t usage,
                                   uint8_t key, uint8_t modifiers,
                                   solar_os_input_key_action_t action)
{
    (void)source; (void)physical_key; (void)usage;
    (void)key; (void)modifiers; (void)action;
    assert(false);
    return ESP_FAIL;
}
uint8_t solar_os_input_composition_apply(uint8_t key) { return key; }
esp_err_t solar_os_log_write(solar_os_log_level_t level, const char *tag,
                             const char *fmt, ...)
{
    (void)level; (void)tag; (void)fmt;
    return ESP_OK;
}
size_t solar_os_input_read_source_chars(solar_os_input_source_t source,
                                        char *buffer, size_t buffer_len)
{
    (void)source; (void)buffer; (void)buffer_len;
    return 0;
}

static void press_for(uint32_t duration_ms, uint8_t expected)
{
    const size_t before = emitted_count;
    pressed = true;
    solar_os_buttons_poll();
    now_ms += 25;
    solar_os_buttons_poll();
    assert(emitted_count == before);
    now_ms += duration_ms - 25;
    pressed = false;
    solar_os_buttons_poll();
    now_ms += 25;
    solar_os_buttons_poll();
    assert(emitted_count == before + 1);
    assert(emitted[before] == expected);
    now_ms += 50;
    solar_os_buttons_poll();
    assert(emitted_count == before + 1);
}

int main(void)
{
    assert(solar_os_buttons_init() == ESP_OK);
    press_for(1000, SOLAR_OS_KEY_ENTER);
    press_for(1199, SOLAR_OS_KEY_ENTER);
    press_for(1200, SOLAR_OS_KEY_ESCAPE);
    press_for(2500, SOLAR_OS_KEY_ESCAPE);
    puts("button_long_press_test: PASS");
    return 0;
}
