#include <assert.h>
#include <stdio.h>
#include "solar_os_bus_types.h"
#include "../../src/drivers/ft6336.c"

static unsigned resets, native_resets, waits;
static bool fail_reset;
static uint8_t report[6];
size_t strlcpy(char *dst, const char *src, size_t size)
{
    size_t len = strlen(src);
    if (size) { size_t n = len < size-1 ? len : size-1; memcpy(dst,src,n); dst[n]=0; }
    return len;
}
void vTaskDelay(TickType_t ticks) { assert(ticks > 0); waits++; }
esp_err_t gpio_config(const gpio_config_t *cfg) { assert(cfg->pin_bit_mask != 0); return ESP_OK; }
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level)
{ assert(pin == 5); if (!level) native_resets++; return ESP_OK; }
esp_err_t solar_os_bus_i2c_read_reg(const char *bus, uint8_t address, uint8_t reg,
                                   uint8_t *data, size_t len)
{
    assert(!strcmp(bus,"i2c0") && address == 0x38);
    if (reg == 0xa3) { assert(len == 1); *data = 0x64; }
    else { assert(reg == 2 && len == 6); memcpy(data,report,6); }
    return ESP_OK;
}
static esp_err_t reset(void *ctx, bool high)
{
    assert(ctx == &fail_reset);
    if (fail_reset) return ESP_FAIL;
    if (!high) resets++;
    return ESP_OK;
}
int main(void)
{
    fail_reset = true;
    assert(ft6336_init_with_reset("i2c0",0x38,4,reset,&fail_reset) == ESP_FAIL);
    ft6336_sample_t sample;
    assert(ft6336_read(&sample) == ESP_ERR_INVALID_STATE);
    fail_reset = false;
    assert(ft6336_init_with_reset("i2c0",0x38,4,reset,&fail_reset) == ESP_OK);
    assert(resets == 1 && native_resets == 0 && waits == 2);
    /* Native portrait bottom-right: 479,799. Preserve the full 12-bit values. */
    memcpy(report,(uint8_t[]){1,1,0xdf,3,0x1f,0},6);
    assert(ft6336_read(&sample) == ESP_OK && sample.touched && sample.x == 479 && sample.y == 799);
    report[0]=0;
    assert(ft6336_read(&sample) == ESP_OK && !sample.touched);
    ft6336_deinit();
    assert(ft6336_init("i2c0",0x38,5,4) == ESP_OK && native_resets == 1);
    ft6336_deinit();
    puts("ft6336_test: PASS");
    return 0;
}
