#include <assert.h>
#include <stdio.h>
#include "solar_os_bus_types.h"
#include "../../src/drivers/ft6336.c"

static unsigned resets, native_resets, waits;
static bool fail_reset;
static uint8_t report[FT6336_FRAME_BYTES];
static unsigned report_reads;
static size_t last_read_length;
static esp_err_t read_error;
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
    else {
        assert(reg == 2 && (len == 6 || len == FT6336_FRAME_BYTES));
        ++report_reads; last_read_length = len;
        if (read_error) { memset(data, 0xa5, 3); return read_error; }
        memcpy(data, report, len);
    }
    return ESP_OK;
}
static esp_err_t reset(void *ctx, bool high)
{
    assert(ctx == &fail_reset);
    if (fail_reset) return ESP_FAIL;
    if (!high) resets++;
    return ESP_OK;
}
static void legacy(void)
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
}

static void legacy_compatibility(void)
{
    assert(ft6336_init_with_reset("i2c0", 0x38, 4, reset, &fail_reset) == ESP_OK);
    ft6336_sample_t sample;
    memcpy(report, (uint8_t[]){2, 1, 0xdf, 0x33, 0x1f, 0, 0, 0, 0, 0, 0, 0, 0}, 13);
    assert(ft6336_read(&sample) == ESP_OK && sample.id == 3 && sample.x == 479 && sample.y == 799);
    assert(last_read_length == 6);
    /* Preserve legacy acceptance of raw nonzero counts, UP/reserved and ID F. */
    report[0] = 15; report[1] |= 0xc0; report[3] = 0xf3;
    assert(ft6336_read(&sample) == ESP_OK && sample.touched && sample.id == 15 && sample.y == 799);
    report[1] = 0x41;
    assert(ft6336_read(&sample) == ESP_OK && sample.touched && sample.x == 479);
    assert(last_read_length == 6);
    const ft6336_sample_t saved = sample;
    read_error = ESP_FAIL;
    assert(ft6336_read(&sample) == ESP_FAIL && memcmp(&saved, &sample, sizeof(sample)) == 0);
    read_error = ESP_OK;
    assert(ft6336_read(NULL) == ESP_ERR_INVALID_ARG);
}
static void reader_two(void)
{
    /* 0x02 status; point 1 at 0x03..08; point 2 at 0x09..0E. */
    memcpy(report, (uint8_t[]){2, 1, 0xdf, 0x33, 0x1f, 5, 6, 0x8f, 0xff, 0x70, 0, 7, 8}, 13);
    ft6336_frame_t frame;
    unsigned before = report_reads;
    assert(ft6336_read_frame(&frame) == ESP_OK && frame.result == FT6336_FRAME_OK);
    assert(report_reads == before + 1 && last_read_length == 13);
    assert(frame.contacts[0].id == 3 && frame.contacts[0].x == 479 && frame.contacts[0].y == 799);
    assert(frame.contacts[1].id == 7 && frame.contacts[1].x == 4095 && frame.contacts[1].y == 0);
    assert(memcmp(frame.raw, report, 13) == 0);
}
static void reader_transport_failure(void)
{
    ft6336_frame_t frame, saved;
    memset(&frame, 0x5a, sizeof(frame)); memcpy(&saved, &frame, sizeof(frame));
    read_error = ESP_FAIL;
    unsigned before = report_reads;
    assert(ft6336_read_frame(&frame) == ESP_FAIL);
    assert(report_reads == before + 1 && last_read_length == 13);
    assert(memcmp(&frame, &saved, sizeof(frame)) == 0);
    read_error = ESP_OK;
}
static void reader_parse_result(void)
{
    ft6336_frame_t frame;
    report[0] = 3;
    assert(ft6336_read_frame(&frame) == ESP_OK && frame.result == FT6336_FRAME_INVALID);
    assert(frame.reported_count == 3 && (frame.reasons & FT6336_FRAME_REASON_COUNT));
    report[0] = 1; report[1] = 0x41;
    assert(ft6336_read_frame(&frame) == ESP_OK && frame.result == FT6336_FRAME_UNCERTAIN);
    assert(frame.contacts[0].event == FT6336_EVENT_UP);
    report[0] = 0;
    assert(ft6336_read_frame(&frame) == ESP_OK && frame.result == FT6336_FRAME_OK && !frame.reported_count);
}
static void reader_arguments_and_state(void)
{
    ft6336_frame_t frame, saved;
    memset(&frame, 0x5a, sizeof(frame)); memcpy(&saved, &frame, sizeof(frame));
    unsigned before = report_reads;
    assert(ft6336_read_frame(NULL) == ESP_ERR_INVALID_ARG);
    ft6336_deinit();
    assert(ft6336_read_frame(&frame) == ESP_ERR_INVALID_STATE);
    assert(report_reads == before && memcmp(&frame, &saved, sizeof(frame)) == 0);
}
int main(void)
{
    legacy(); legacy_compatibility(); reader_two(); reader_transport_failure();
    reader_parse_result(); reader_arguments_and_state();
    puts("ft6336_test: 6 cases PASS (legacy + complete-frame reader)");
    return 0;
}
