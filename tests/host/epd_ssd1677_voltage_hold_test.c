/* Exercise the real SSD1677 driver against the captured SPI fixture. */
#define main epd_ssd1677_baseline_main
#include "epd_ssd1677_test.c"
#undef main

#if !SOLAR_OS_SSD1677_VOLTAGE_HOLD_TRIAL || !SOLAR_OS_SSD1677_DIRTY_HOLD_TRIAL
#error Voltage A/B fixture requires both opt-in paths
#endif

static unsigned occurrences(uint8_t command)
{
    unsigned n = 0;
    for (size_t i = 0; i < command_count; ++i)
        if (commands[i].command == command) ++n;
    return n;
}

static uint64_t baseline_non_voltage_hash[6];

static uint64_t non_voltage_hash(void)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    for (size_t i = 0; i < command_count; ++i) {
        const captured_command_t *c = &commands[i];
        if (c->command == 0x03 || c->command == 0x04 || c->command == 0x2c)
            continue;
        hash = (hash ^ c->command) * UINT64_C(1099511628211);
        for (size_t b = 0; b < c->length; ++b)
            hash = (hash ^ c->data[b]) * UINT64_C(1099511628211);
    }
    return hash;
}

static void expect_fast_sequence(bool cold, bool hold)
{
    const unsigned voltage_count = hold ? 0U : 1U;
    assert(occurrences(0x32) == 1);
    assert(occurrences(0x03) == voltage_count);
    assert(occurrences(0x04) == voltage_count);
    assert(occurrences(0x2c) == voltage_count);
    assert(occurrences(0x21) >= 1 && occurrences(0x22) >= 1);
    if (!cold) assert(occurrences(0x21) == 1 && occurrences(0x22) == 1);
    const captured_command_t *update = find_command(0x22, occurrences(0x22) - 1U);
    assert(update->length == 1 && update->data[0] == (cold ? 0xcc : 0x0c));
    assert(occurrences(0x20) == 1);
    size_t lut = 0, last_voltage = 0, ctrl1 = 0, ctrl2 = 0, activation = 0;
    for (size_t i = 0; i < command_count; ++i) {
        switch (commands[i].command) {
        case 0x32: lut = i; break;
        case 0x2c: last_voltage = i; break;
        case 0x21: ctrl1 = i; break;
        case 0x22: ctrl2 = i; break;
        case 0x20: activation = i; break;
        default: break;
        }
    }
    assert(lut < ctrl1 && ctrl1 < ctrl2 && ctrl2 < activation);
    if (!hold) assert(lut < last_voltage && last_voltage < ctrl1);
}

static void test_condition(bool hold)
{
    epd_ssd1677_t d = make_display();
    d.papermono_otp = true;
    d.band_buffer = band;
    d.fastest_tiles = true;
    d.fastest_spi_hz = d.configured_spi_hz = 20000000;
    assert(epd_ssd1677_set_controller_mode(&d, "dirty-window=rows-dual") == ESP_OK);
    assert(epd_ssd1677_set_controller_mode(&d, "refresh=fastest") == ESP_OK);
    assert(epd_ssd1677_set_controller_mode(&d,
           hold ? "voltage-hold=on" : "voltage-hold=off") == ESP_OK);
    assert(ssd1677_refresh(&d) == ESP_OK); /* OTP baseline */
    assert(!d.voltage_hold_ready);
    command_count = 0;
    assert(ssd1677_refresh(&d) == ESP_OK && command_count == 0);
    d.trial_force_identical_fastest = true;
    command_count = 0;
    assert(ssd1677_refresh(&d) == ESP_OK); /* common first custom update */
    expect_fast_sequence(true, false);
    assert(d.voltage_hold_ready && d.trial_fast_0x20_count == 1);
    const uint8_t initial = frame[0];
    for (unsigned step = 1; step <= 6; ++step) {
        command_count = 0;
        assert(ssd1677_refresh(&d) == ESP_OK);
        expect_fast_sequence(false, hold);
        const uint64_t signature = non_voltage_hash();
        if (hold) assert(signature == baseline_non_voltage_hash[step - 1U]);
        else baseline_non_voltage_hash[step - 1U] = signature;
        assert(d.trial_fast_0x20_count == step + 1);
        assert(frame[0] == initial && !memcmp(frame, shadow, sizeof(frame)));
        assert(d.dirty_hold_trial && d.dirty_hold_dual_sync && d.dirty_hold_ram_synced);
    }
    d.trial_force_identical_fastest = false;
    command_count = 0;
    assert(ssd1677_refresh(&d) == ESP_OK && command_count == 0);

    /* A completed OTP update must discard the register-retention assumption. */
    assert(epd_ssd1677_set_controller_mode(&d, "refresh=cleanup") == ESP_OK);
    command_count = 0;
    assert(ssd1677_refresh(&d) == ESP_OK && !d.voltage_hold_ready);
    assert(epd_ssd1677_set_controller_mode(&d, "refresh=fastest") == ESP_OK);
    d.trial_force_identical_fastest = true;
    command_count = 0;
    assert(ssd1677_refresh(&d) == ESP_OK);
    expect_fast_sequence(true, false);
    assert(ssd1677_sleep(&d) == ESP_OK && !d.voltage_hold_ready);
    assert(ssd1677_controller_init(&d) == ESP_OK && !d.voltage_hold_ready);
}

static void test_failure_invalidates_hold(void)
{
    epd_ssd1677_t d = make_display();
    d.papermono_otp = true;
    d.band_buffer = band;
    d.fastest_tiles = true;
    d.fastest_spi_hz = d.configured_spi_hz = 20000000;
    assert(epd_ssd1677_set_controller_mode(&d, "refresh=fastest") == ESP_OK);
    assert(epd_ssd1677_set_controller_mode(&d, "voltage-hold=on") == ESP_OK);
    assert(ssd1677_refresh(&d) == ESP_OK);
    d.trial_force_identical_fastest = true;
    assert(ssd1677_refresh(&d) == ESP_OK && d.voltage_hold_ready);
    fail_command = 0x20;
    command_count = 0;
    assert(ssd1677_refresh(&d) != ESP_OK);
    assert(!d.voltage_hold_ready && !d.reference_awake && d.faulted);
}

int main(void)
{
    test_condition(false);
    test_condition(true);
    test_failure_invalidates_hold();
    puts("epd_ssd1677_voltage_hold_test: PASS");
    return 0;
}
