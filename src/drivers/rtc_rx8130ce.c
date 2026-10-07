/* EPSON RX8130CE ETM50E-10: 0x10..16 calendar, 0x1d flags,
 * 0x1e control 0, 0x1f control 1. Hardware calendar stores UTC. */
#include "rtc_rx8130ce.h"
#include "solar_os_buses.h"
#include <string.h>

#define RX_VLF 0x02U
#define RX_STOP 0x40U

static bool unbcd(uint8_t v, uint8_t *out)
{
    if ((v & 15) > 9 || (v >> 4) > 9) return false;
    *out = (v >> 4) * 10 + (v & 15);
    return true;
}
static uint8_t bcd(unsigned v) { return (v / 10) * 16 + v % 10; }

typedef struct { rtc_rx8130ce_t *rtc; solar_os_datetime_t *dt; } get_request_t;
static esp_err_t get_session(const solar_os_i2c_transaction_t *s, void *user)
{
    get_request_t *r = user;
    uint8_t raw[7], controls[3];
    esp_err_t err = s->read_reg(s->context, r->rtc->address, 0x1d, controls, 3);
    if (err != ESP_OK) return err;
    err = s->read_reg(s->context, r->rtc->address, 0x10, raw, 7);
    if (err != ESP_OK) return err;
    solar_os_datetime_t dt = {0};
    uint8_t year;
    /* Reject reserved calendar bits as well as invalid BCD digits. */
    if ((raw[0] & 0x80) || (raw[1] & 0x80) || (raw[2] & 0xc0) ||
        (raw[3] & 0x80) || (raw[4] & 0xc0) || (raw[5] & 0xe0) ||
        !unbcd(raw[0], &dt.second) || !unbcd(raw[1], &dt.minute) ||
        !unbcd(raw[2], &dt.hour) || !unbcd(raw[4], &dt.day) ||
        !unbcd(raw[5], &dt.month) || !unbcd(raw[6], &year) ||
        raw[3] == 0 || (raw[3] & (raw[3] - 1)) != 0)
        return ESP_ERR_INVALID_STATE;
    dt.year = 2000 + year;
    for (dt.weekday = 0; (raw[3] >> dt.weekday) != 1; dt.weekday++) {}
    if (!solar_os_time_datetime_is_valid(&dt)) return ESP_ERR_INVALID_STATE;
    /* TEST mode also invalidates normal timekeeping. VBLF is a voltage
     * warning, not evidence that the oscillator stopped; VLF is definitive. */
    dt.clock_integrity = !(controls[0] & RX_VLF) && !(controls[1] & (RX_STOP | 0x80))
        && !r->rtc->write_failed;
    *r->dt = dt;
    return ESP_OK;
}

esp_err_t rtc_rx8130ce_get(rtc_rx8130ce_t *rtc, solar_os_datetime_t *dt)
{
    if (rtc == NULL || dt == NULL) return ESP_ERR_INVALID_ARG;
    memset(dt, 0, sizeof(*dt));
    if (!rtc->ready) return ESP_ERR_INVALID_STATE;
    get_request_t request = {rtc, dt};
    return solar_os_bus_i2c_transaction(rtc->bus, get_session, &request);
}

esp_err_t rtc_rx8130ce_init(rtc_rx8130ce_t *rtc, const char *bus, uint8_t addr)
{
    if (rtc == NULL || bus == NULL || !bus[0] || strlen(bus) >= sizeof(rtc->bus) ||
        addr != RTC_RX8130CE_ADDRESS) return ESP_ERR_INVALID_ARG;
    memset(rtc, 0, sizeof(*rtc));
    uint8_t control;
    esp_err_t err = solar_os_bus_i2c_read_reg(bus, addr, 0x1f, &control, 1);
    if (err != ESP_OK) return err;
    /* Initialize backup switching; preserve CHGEN and every unrelated bit.
     * PaperMono VDD/VBAT are both on 3V3_L0. No backup charging is enabled. */
    if (!(control & 0x10)) {
        control |= 0x10;
        err = solar_os_bus_i2c_write_reg(bus, addr, 0x1f, &control, 1);
        if (err != ESP_OK) return err;
    }
    strlcpy(rtc->bus, bus, sizeof(rtc->bus));
    rtc->address = addr;
    rtc->ready = true; /* Invalid calendar remains queryable/settable. */
    return ESP_OK;
}

typedef struct { rtc_rx8130ce_t *rtc; const solar_os_datetime_t *dt; } set_request_t;
static esp_err_t set_session(const solar_os_i2c_transaction_t *s, void *user)
{
    set_request_t *r = user;
    uint8_t ctl;
    esp_err_t err = s->read_reg(s->context, r->rtc->address, 0x1e, &ctl, 1);
    if (err != ESP_OK) return err;
    /* Never set time in factory TEST mode. */
    if (ctl & 0x80) return ESP_ERR_INVALID_STATE;
    const uint8_t stop = ctl | RX_STOP, run = ctl & ~RX_STOP;
    r->rtc->write_failed = true;
    err = s->write_reg(s->context, r->rtc->address, 0x1e, &stop, 1);
    if (err == ESP_OK) {
        const solar_os_datetime_t *dt = r->dt;
        const uint8_t raw[] = {bcd(dt->second), bcd(dt->minute), bcd(dt->hour),
            1U << dt->weekday, bcd(dt->day), bcd(dt->month), bcd(dt->year - 2000)};
        err = s->write_reg(s->context, r->rtc->address, 0x10, raw, sizeof(raw));
    }
    /* STOP may have reached the chip even if I2C reported an error. Always
     * attempt to restart. Preserve IRQ enables and every unrelated bit. */
    esp_err_t restart = s->write_reg(s->context, r->rtc->address, 0x1e, &run, 1);
    if (err != ESP_OK) return err;
    if (restart != ESP_OK) return restart;
    /* Flags are W0C: write ones to all other flags (including concurrently
     * asserted events), clearing only VLF after a successful calendar write. */
    const uint8_t flags = 0xbd; /* Preserve W0C flags; reserved bit 6 stays 0. */
    err = s->write_reg(s->context, r->rtc->address, 0x1d, &flags, 1);
    if (err == ESP_OK) r->rtc->write_failed = false;
    return err;
}

esp_err_t rtc_rx8130ce_set(rtc_rx8130ce_t *rtc, const solar_os_datetime_t *dt)
{
    if (rtc == NULL || dt == NULL || !solar_os_time_datetime_is_valid(dt) ||
        dt->year < 2000 || dt->year > 2099 || dt->weekday > 6)
        return ESP_ERR_INVALID_ARG;
    if (!rtc->ready) return ESP_ERR_INVALID_STATE;
    set_request_t request = {rtc, dt};
    return solar_os_bus_i2c_transaction(rtc->bus, set_session, &request);
}
