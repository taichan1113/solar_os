#pragma once
#include "solar_os_time.h"
#include "solar_os_bus_types.h"

#define RTC_RX8130CE_ADDRESS 0x32
typedef struct {
    char bus[SOLAR_OS_BUS_NAME_MAX];
    uint8_t address;
    bool ready;
    bool write_failed;
} rtc_rx8130ce_t;

esp_err_t rtc_rx8130ce_init(rtc_rx8130ce_t *, const char *, uint8_t);
esp_err_t rtc_rx8130ce_get(rtc_rx8130ce_t *, solar_os_datetime_t *);
esp_err_t rtc_rx8130ce_set(rtc_rx8130ce_t *, const solar_os_datetime_t *);
