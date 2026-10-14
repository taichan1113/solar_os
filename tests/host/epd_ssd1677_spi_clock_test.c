/* Exercise the real SSD1677 clock selection with the established SPI mock. */
#define main epd_ssd1677_baseline_main
#include "epd_ssd1677_test.c"
#undef main

int main(void)
{
    const int expected = SOLAR_OS_SSD1677_SPI20_TRIAL ? 20000000 : 40000000;
    epd_ssd1677_t d = make_display();
    d.papermono_otp = true;
    d.band_buffer = band;
    d.fastest_tiles = true;
    d.configured_spi_hz = 10000000;
    d.fastest_spi_hz = ssd1677_default_fastest_spi_hz();
#if SOLAR_OS_SSD1677_DIRTY_HOLD_TRIAL
    d.dirty_hold_trial = true;
#endif
    assert(d.fastest_spi_hz == expected);
    assert(strstr(epd_ssd1677_controller_mode_values(&d),
                  SOLAR_OS_SSD1677_SPI20_TRIAL ? "spi-mhz=<10,20>" :
                                                  "spi-mhz=<10,20,40>") != NULL);
    assert(epd_ssd1677_set_controller_mode(&d, "refresh=fastest") == ESP_OK);
    assert(ssd1677_refresh(&d) == ESP_OK); /* OTP baseline uses board clock. */
    assert(d.actual_spi_hz == 10000000);

    frame[0] ^= 1;
    command_count = 0;
    assert(ssd1677_refresh(&d) == ESP_OK);
    assert(d.actual_spi_hz == expected);
    assert(find_command(0x24, 0)->length == SSD1677_BUFFER_BYTES);
    frame[1] ^= 1;
    command_count = 0;
    assert(ssd1677_refresh(&d) == ESP_OK);
    assert(d.actual_spi_hz == expected);
    assert(find_command(0x24, 0)->length ==
           (SOLAR_OS_SSD1677_DIRTY_HOLD_TRIAL ? 1U : SSD1677_BUFFER_BYTES));

    assert(epd_ssd1677_set_controller_mode(&d, "spi-mhz=20") == ESP_OK);
    assert(d.fastest_spi_hz == 20000000);
    assert(epd_ssd1677_set_controller_mode(&d, "spi-mhz=40") ==
           (SOLAR_OS_SSD1677_SPI20_TRIAL ? ESP_ERR_INVALID_ARG : ESP_OK));
    assert(d.fastest_spi_hz == expected);
    assert(epd_ssd1677_set_controller_mode(&d, "refresh=auto") == ESP_OK);
    frame[2] ^= 1;
    assert(ssd1677_refresh(&d) == ESP_OK);
    assert(d.actual_spi_hz == 10000000);
    assert(epd_ssd1677_set_controller_mode(&d, "refresh=fastest") == ESP_OK);
    frame[3] ^= 1;
    assert(ssd1677_refresh(&d) == ESP_OK); /* Mode-switch fallback. */
    frame[4] ^= 1;
    assert(ssd1677_refresh(&d) == ESP_OK && d.actual_spi_hz == expected);

    d.reference_awake = false; /* Controller reset / unknown retained RAM. */
    frame[5] ^= 1;
    command_count = 0;
    assert(ssd1677_refresh(&d) == ESP_OK && d.actual_spi_hz == expected);
    assert(find_command(0x24, 0)->length == SSD1677_BUFFER_BYTES);

    frame[6] ^= 1;
    command_count = 0;
    fail_command = 0x24;
    assert(ssd1677_refresh(&d) != ESP_OK);
    assert(d.faulted && !d.shadow_valid);
    const size_t failed_count = command_count;
    assert(ssd1677_refresh(&d) == ESP_ERR_INVALID_STATE);
    assert(command_count == failed_count);

    d = make_display();
    d.papermono_otp = true;
    d.band_buffer = band;
    d.fastest_tiles = true;
    d.configured_spi_hz = 10000000;
    d.fastest_spi_hz = ssd1677_default_fastest_spi_hz();
    assert(epd_ssd1677_set_controller_mode(&d, "refresh=fastest") == ESP_OK);
    assert(ssd1677_refresh(&d) == ESP_OK);
    frame[0] ^= 1;
    fail_spi_override = true;
    assert(ssd1677_refresh(&d) != ESP_OK);
    assert(d.faulted && !d.shadow_valid);
    fail_spi_override = false;
    assert(ssd1677_refresh(&d) == ESP_ERR_INVALID_STATE);
    puts("epd_ssd1677_spi_clock_test: PASS");
    return 0;
}
