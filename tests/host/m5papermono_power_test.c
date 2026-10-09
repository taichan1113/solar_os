#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../src/services/solar_os_m5papermono_power.c"

static uint8_t pm_regs[256], ioe_regs[256];
static int64_t now_us;
static unsigned reads;
static solar_os_battery_provider_t registered_battery;
static solar_os_status_led_provider_t registered_led;
static unsigned writes, registrations;
static int fail_addr = -1, fail_reg = -1;
static bool leased;

size_t strlcpy(char *dst, const char *src, size_t size)
{
    size_t len = strlen(src);
    if (size) { size_t n = len < size - 1 ? len : size - 1; memcpy(dst, src, n); dst[n] = 0; }
    return len;
}
void vTaskDelay(TickType_t ticks) { assert(ticks > 0); }
esp_err_t solar_os_bus_i2c_read_reg(const char *bus, uint8_t addr, uint8_t reg,
                                   uint8_t *data, size_t len)
{
    /* Optional charger access must not reconnect its hazardous bus branch. */
    assert(!strcmp(bus, "i2c0") && (addr == 0x6e || addr == 0x4f));
    reads++;
    if (addr == fail_addr && reg == fail_reg) return ESP_FAIL;
    memcpy(data, (addr == 0x6e ? pm_regs : ioe_regs) + reg, len);
    return ESP_OK;
}
int64_t esp_timer_get_time(void) { return now_us; }
static esp_err_t session_read(void *ctx, uint8_t a, uint8_t r, uint8_t *p, size_t n)
{ (void)ctx; return solar_os_bus_i2c_read_reg("i2c0", a, r, p, n); }
static esp_err_t session_write(void *ctx, uint8_t a, uint8_t r, const uint8_t *p, size_t n)
{ (void)ctx; return solar_os_bus_i2c_write_reg("i2c0", a, r, p, n); }
esp_err_t solar_os_bus_i2c_transaction(const char *bus,
    esp_err_t (*cb)(const solar_os_i2c_transaction_t *, void *), void *user)
{
    assert(!strcmp(bus, "i2c0"));
    const solar_os_i2c_transaction_t s = {.read_reg = session_read, .write_reg = session_write};
    return cb(&s, user);
}
esp_err_t solar_os_battery_register_provider(const char *name, const solar_os_battery_provider_t *p)
{ assert(!strcmp(name,"ioe0")); registered_battery = *p; return ESP_OK; }
esp_err_t solar_os_battery_unregister_provider(const char *name)
{ assert(!strcmp(name,"ioe0")); memset(&registered_battery,0,sizeof(registered_battery)); return ESP_OK; }
esp_err_t solar_os_status_led_register_provider(const char *name, const solar_os_status_led_provider_t *p)
{ assert(!strcmp(name,"ioe0")); registered_led = *p; return ESP_OK; }
esp_err_t solar_os_status_led_unregister_provider(const char *name)
{ assert(!strcmp(name,"ioe0")); memset(&registered_led,0,sizeof(registered_led)); return ESP_OK; }
esp_err_t solar_os_bus_i2c_write_reg(const char *bus, uint8_t addr, uint8_t reg,
                                    const uint8_t *data, size_t len)
{
    assert(!strcmp(bus, "i2c0") && (addr == 0x6e || addr == 0x4f));
    if (addr == fail_addr && reg == fail_reg) return ESP_FAIL;
    writes++;
    memcpy((addr == 0x6e ? pm_regs : ioe_regs) + reg, data, len);
    return ESP_OK;
}
esp_err_t solar_os_gpio_controller_register(const solar_os_gpio_controller_registration_t *r)
{
    assert(!strcmp(r->name, "ioe0") && r->line_count == 14 && r->ops == &ops);
    registrations++;
    return ESP_OK;
}
esp_err_t solar_os_gpio_controller_unregister(const char *name)
{
    assert(!strcmp(name, "ioe0"));
    return leased ? ESP_ERR_INVALID_STATE : ESP_OK;
}
static const solar_os_expansion_binding_t bindings[] = {
    {.kind = SOLAR_OS_EXPANSION_BINDING_I2C_BUS, .target = "i2c0"},
    {.kind = SOLAR_OS_EXPANSION_BINDING_I2C_ADDRESS, .value = 0x4f},
    {.kind = SOLAR_OS_EXPANSION_BINDING_I2C_ADDRESS, .value = 0x6e, .role = "pm"},
};
int main(void)
{
    /* Wrong PMIC: no register writes, no GPIO provider, no panel activation. */
    assert(attach("ioe0", bindings, 3) == ESP_ERR_NOT_SUPPORTED);
    assert(!writes && !registrations && !power.active);
    pm_regs[0] = 0x50; pm_regs[1] = 0x20;
    fail_addr = 0x6e; fail_reg = 0x09;
    assert(attach("ioe0", bindings, 3) == ESP_FAIL);
    assert(!writes && !registrations);
    fail_addr = -1;
    memset(ioe_regs, 0xa5, sizeof(ioe_regs));
    pm_regs[6] = 0xa0;
    assert(attach("ioe0", bindings, 3) == ESP_OK);
    assert(pm_regs[6] == 0xa7 && pm_regs[9] == 0 && pm_regs[10] == 0);
    assert((ioe_regs[5] & 0x80) == 0 && (ioe_regs[6] & 1) == 0);
    assert((ioe_regs[0x1c] & 0x80) == 0 && (ioe_regs[0x1e] & 0x80) == 0);
    bool on;
    assert(registered_led.get(NULL,&on) == ESP_OK && !on);
    assert(registered_led.set(NULL,true) == ESP_OK && pm_regs[6] == 0xb7);
    assert(registered_led.set(NULL,false) == ESP_OK && pm_regs[6] == 0xa7);
    fail_addr = 0x6e; fail_reg = 6;
    assert(registered_led.set(NULL,true) == ESP_FAIL && pm_regs[6] == 0xa7);
    fail_addr = -1;
    pm_regs[0x22] = 0x68; pm_regs[0x23] = 0x10; /* 4200mV needs >12 bits. */
    pm_regs[4] = 5;
    const uint8_t mux_latch = ioe_regs[6];
    unsigned writes_before_sample = writes;
    solar_os_battery_sample_t sample;
    assert(registered_battery.read(NULL,&sample) == ESP_OK);
    assert(sample.battery_mv == 4200 && !sample.percent_unavailable && !sample.percent_valid);
    assert(registered_battery.estimate_percent != NULL);
    const uint16_t voltage_cases[] = {0, 3270, 3300, 3301, 3749, 3750, 4199, 4200, 4300, UINT16_MAX};
    const uint8_t expected_percent[] = {0, 0, 0, 0, 49, 50, 99, 100, 100, 100};
    for (size_t i = 0; i < sizeof(voltage_cases) / sizeof(voltage_cases[0]); i++) {
        assert(registered_battery.estimate_percent(NULL, voltage_cases[i]) == expected_percent[i]);
    }
    assert(sample.external_power_valid && sample.external_power && !sample.charging_valid);
    assert(!sample.charge_complete_valid && !sample.charge_complete);
    assert(writes == writes_before_sample && ioe_regs[6] == mux_latch && !(mux_latch & 4));
    unsigned before = reads;
    assert(registered_battery.read(NULL,&sample) == ESP_OK && reads == before);
    /* PM1 rail control is not evidence of the separate charger's state. */
    now_us += 30000000; pm_regs[6] &= (uint8_t)~1U;
    assert(registered_battery.read(NULL,&sample) == ESP_OK && !sample.charging_valid);
    assert(sample.battery_mv == 4200 && sample.external_power_valid && sample.external_power);
    assert(!sample.percent_unavailable && !sample.charge_complete_valid);
    assert(writes == writes_before_sample && ioe_regs[6] == mux_latch);
    /* Source-read faults still invalidate the measurement, with no fallback. */
    now_us += 30000000; fail_addr = 0x6e; fail_reg = 0x04;
    assert(registered_battery.read(NULL,&sample) == ESP_FAIL);
    fail_addr = -1; now_us += 30000000; pm_regs[4] = 4;
    assert(registered_battery.read(NULL,&sample) == ESP_OK && !sample.external_power && !sample.charging && sample.charging_valid);
    fail_addr = 0x6e; fail_reg = 0x22; now_us += 30000000;
    assert(registered_battery.read(NULL,&sample) != ESP_OK);
    before = reads;
    assert(registered_battery.read(NULL,&sample) != ESP_OK && reads == before);
    fail_addr = -1;
    assert(registrations == 1 && ioe_regs[0x23] == 0);
    assert((ioe_regs[6] & 4) == 0 && (ioe_regs[4] & 4) != 0);
    /* Every unrelated output bit must survive EPD and touch writes. */
    uint8_t old = ioe_regs[5];
    assert(write_line(NULL, 2, true) == ESP_OK);
    assert(ioe_regs[5] == (old | 4));
    assert((ioe_regs[3] & 4) != 0 && (ioe_regs[0x13] & 4) == 0);
    old = ioe_regs[6];
    assert(write_line(NULL, 12, true) == ESP_OK);
    assert(ioe_regs[6] == (old | 0x10));
    fail_addr = 0x4f; fail_reg = 5;
    assert(write_line(NULL, 4, false) == ESP_FAIL);
    assert(write_line(NULL, 14, true) == ESP_ERR_INVALID_ARG);
    fail_addr = -1;
    leased = true;
    assert(detach("ioe0") == ESP_ERR_INVALID_STATE && power.active);
    leased = false;
    writes = 0;
    assert(detach("ioe0") == ESP_OK && writes == 0);
    puts("m5papermono_power_test: PASS");
    return 0;
}
