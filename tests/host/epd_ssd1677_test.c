#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Test the real command/refresh implementation with a captured SPI transport. */
#include "../../src/drivers/epd_ssd1677.c"

typedef struct {
    uint8_t command;
    size_t length;
    uint8_t data[SSD1677_BUFFER_BYTES];
} captured_command_t;

static captured_command_t commands[128];
static size_t command_count;
static int dc_level, reset_level = 1;
static int reset_count, power_count;
static bool stuck_busy;
static bool fail_external_reset;
static int fail_command = -1;
static bool busy_on_activation;
static bool fail_plane_data;
static int64_t time_us;
static int64_t spi_delay_us;
static uint8_t frame[SSD1677_BUFFER_BYTES], shadow[SSD1677_BUFFER_BYTES];
static uint8_t line[SSD1677_PANEL_ROW_BYTES];
static bool mutate_render_buffer;
static int actual_spi_hz = 10000000;
static uint8_t band[3200];

esp_err_t spi_device_get_actual_freq(spi_device_handle_t device, int *khz)
{
    assert(device != NULL);
    *khz = actual_spi_hz/1000;
    return ESP_OK;
}

void vTaskDelay(TickType_t ticks)
{
    assert(ticks > 0);
    time_us += (int64_t)ticks * SOLAR_OS_EPD_TEST_TICK_MS * 1000;
}

int64_t esp_timer_get_time(void) { return time_us; }
int gpio_get_level(gpio_num_t pin) { (void)pin; return stuck_busy ? 1 : 0; }
esp_err_t gpio_config(const gpio_config_t *config) { (void)config; return ESP_OK; }

esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level)
{
    if (pin == 9) dc_level = (int)level;
    if (pin == 46) {
        reset_level = (int)level;
        if (level == 0) reset_count++;
    }
    return ESP_OK;
}

esp_err_t spi_device_polling_transmit(spi_device_handle_t device, spi_transaction_t *transaction)
{
    assert(device != NULL);
    if (transaction->override_freq_hz) actual_spi_hz = (int)transaction->override_freq_hz;
    time_us += spi_delay_us;
    const uint8_t *data = transaction->flags & SPI_TRANS_USE_TXDATA ?
        transaction->tx_data : transaction->tx_buffer;
    size_t length = transaction->length / 8U;
    if (dc_level == 0) {
        if (data[0] == fail_command) return ESP_FAIL;
        if (data[0] == 0x20 && busy_on_activation) stuck_busy = true;
        assert(length == 1 && command_count < 128);
        commands[command_count++] = (captured_command_t){.command = data[0]};
    } else {
        if (mutate_render_buffer) { memset(frame, 0x55, sizeof(frame)); mutate_render_buffer=false; }
        assert(command_count > 0);
        captured_command_t *command = &commands[command_count - 1U];
        if (fail_plane_data && command->command == 0x24 && command->length >= 100)
            return ESP_FAIL;
        assert(command->length + length <= sizeof(command->data));
        memcpy(command->data + command->length, data, length);
        command->length += length;
    }
    return ESP_OK;
}

static esp_err_t set_power(void *context, bool on)
{
    (void)context;
    (void)on;
    power_count++;
    return ESP_OK;
}

static epd_ssd1677_t make_display(void)
{
    memset(frame, 0xff, sizeof(frame)); /* white */
    memset(shadow, 0, sizeof(shadow));
    command_count = 0;
    fail_command = -1;
    busy_on_activation = false;
    fail_plane_data = false;
    return (epd_ssd1677_t) {
        .spi = (void *)1, .buffer = frame, .shadow = shadow, .line_buffer = line,
        .buffer_size = sizeof(frame), .shadow_size = sizeof(shadow),
        .line_buffer_size = sizeof(line), .dc_pin = 9, .reset_pin = 46,
        .busy_pin = 3, .busy_level = 1, .power_pin = -1,
        .set_power = set_power,
    };
}

static captured_command_t *find_command(uint8_t command, size_t occurrence)
{
    for (size_t i = 0; i < command_count; i++) {
        if (commands[i].command == command && occurrence-- == 0) return &commands[i];
    }
    assert(!"missing command");
    return NULL;
}

static void expect_data(uint8_t command, const uint8_t *data, size_t length)
{
    captured_command_t *capture = find_command(command, 0);
    assert(capture->length == length && memcmp(capture->data, data, length) == 0);
}

static void test_initial_full(void)
{
    epd_ssd1677_t display = make_display();
    /* Three distinct marks prove conversion order and bottom/top gate mapping. */
    frame[0] &= (uint8_t)~1U;
    frame[799] &= (uint8_t)~1U;
    frame[sizeof(frame) - 1U] &= (uint8_t)~0x80U;
    assert(ssd1677_refresh(&display) == ESP_OK);
    assert(power_count == 1 && reset_count == 1 && reset_level == 1);
    expect_data(0x11, (uint8_t[]){0x01}, 1);
    expect_data(0x44, (uint8_t[]){0, 0, 0x1f, 3}, 4);
    expect_data(0x45, (uint8_t[]){0xdf, 1, 0, 0}, 4);
    expect_data(0x4f, (uint8_t[]){0xdf, 1}, 2);
    expect_data(0x46, (uint8_t[]){0xf7}, 1);
    expect_data(0x47, (uint8_t[]){0xf7}, 1);
    expect_data(0x21, (uint8_t[]){0}, 1);
    expect_data(0x22, (uint8_t[]){0xfc}, 1);
    assert(find_command(0x3c, 1)->data[0] == 0xc0);
    const captured_command_t *current = find_command(0x24, 0);
    const captured_command_t *baseline = find_command(0x26, 0);
    assert(current->length == sizeof(frame) && baseline->length == sizeof(frame));
    assert(current->data[0] == 0x7f && current->data[99] == 0xfe);
    assert(current->data[sizeof(frame) - 1U] == 0xfe);
    for (size_t i = 0; i < sizeof(frame); i++) {
        assert((current->data[i] ^ baseline->data[i]) == 0xff);
    }
    assert(memcmp(find_command(0x24, 1)->data, current->data, sizeof(frame)) == 0);
    assert(memcmp(find_command(0x26, 1)->data, current->data, sizeof(frame)) == 0);
    assert(display.shadow_valid && memcmp(frame, shadow, sizeof(frame)) == 0);
    command_count = 0;
    assert(ssd1677_refresh(&display) == ESP_OK && command_count == 0);
}

static void test_partial_and_cleanup(void)
{
    epd_ssd1677_t display = make_display();
    display.controller_ready = true;
    display.shadow_valid = true;
    memcpy(shadow, frame, sizeof(frame));
    /* Dirty byte at x=16..23, logical y=10: controller gate Y=469. */
    frame[800 + 17] &= (uint8_t)~4U;
    const int resets_before = reset_count;
    assert(ssd1677_refresh(&display) == ESP_OK);
    assert(reset_count == resets_before);
    expect_data(0x44, (uint8_t[]){16, 0, 23, 0}, 4);
    expect_data(0x45, (uint8_t[]){0xd5, 1, 0xd5, 1}, 4);
    expect_data(0x4f, (uint8_t[]){0xd5, 1}, 2);
    expect_data(0x24, (uint8_t[]){0xbf}, 1);
    expect_data(0x26, (uint8_t[]){0xff}, 1);
    assert(find_command(0x26, 1)->data[0] == 0xbf);
    assert(display.partial_refresh_count == 1);

    /* Periodic cleanup must force a full transition without resetting RAM. */
    display.partial_refresh_count = SSD1677_AUTO_FULL_INTERVAL - 1U;
    frame[0] &= (uint8_t)~1U;
    command_count = 0;
    assert(ssd1677_refresh(&display) == ESP_OK);
    assert(find_command(0x24, 0)->length == sizeof(frame));
    assert(find_command(0x26, 0)->data[0] == 0x80);
    assert(display.partial_refresh_count == 0 && reset_count == resets_before);
}

static void test_failure_and_no_shadow(void)
{
    epd_ssd1677_t display = make_display();
    display.controller_ready = true;
    display.shadow_valid = true;
    memcpy(shadow, frame, sizeof(frame));
    frame[0] &= (uint8_t)~1U;
    stuck_busy = true;
    assert(ssd1677_refresh(&display) == ESP_ERR_TIMEOUT);
    assert(!display.shadow_valid && !display.controller_ready);
    assert(shadow[0] == 0xff);
    stuck_busy = false;

    display = make_display();
    display.controller_ready = true;
    display.shadow = NULL;
    display.shadow_valid = true;
    assert(ssd1677_refresh(&display) == ESP_OK);
    assert(find_command(0x24, 0)->length == sizeof(frame));
    assert(find_command(0x26, 0)->data[0] == 0);
}

static esp_err_t external_reset(void *context, bool high)
{
    assert(context == &fail_external_reset);
    if (fail_external_reset) return ESP_FAIL;
    reset_level = high;
    if (!high) reset_count++;
    return ESP_OK;
}

static void test_papermono_otp(void)
{
    epd_ssd1677_t display = make_display();
    display.papermono_otp = true;
    display.set_reset = external_reset;
    display.reset_context = &fail_external_reset;
    display.reset_pin = -1;
    frame[0] &= (uint8_t)~1U;
    assert(ssd1677_refresh(&display) == ESP_OK);
    expect_data(0x0c, (uint8_t[]){0xae, 0xc7, 0xc3, 0xc0, 0x80}, 5);
    expect_data(0x3c, (uint8_t[]){1}, 1);
    expect_data(0x22, (uint8_t[]){0xf8}, 1);
    assert(find_command(0x22, 1)->data[0] == 0x14);
    expect_data(0x10, (uint8_t[]){1}, 1);
    assert(find_command(0x24, 0)->data[0] == 0x80);
    assert(find_command(0x24, 1)->data[0] == 0x7f);
    assert(find_command(0x26, 0)->data[0] == 0x7f);
    size_t activations = 0;
    for (size_t i = 0; i < command_count; i++) {
        assert(commands[i].command != 0x32); /* Never load a custom LUT. */
        assert(commands[i].command != 0x03 && commands[i].command != 0x04);
        if (commands[i].command == 0x20) activations++;
    }
    assert(activations == 2 && !display.controller_ready && display.shadow_valid);
    assert(display.papermono_baseline_valid && display.powered);
    assert(epd_ssd1677_set_controller_mode(&display, "refresh=full") == ESP_OK);
    command_count = 0;
    assert(ssd1677_refresh(&display) == ESP_OK && command_count == 0);
    /* Explicit full retains the original two-activation OTP path. */
    frame[0] = 0;
    assert(ssd1677_refresh(&display) == ESP_OK);
    assert(find_command(0x24, 0)->length == sizeof(frame));
    command_count = 0;
    frame[0] = 1;
    stuck_busy = true;
    assert(ssd1677_refresh(&display) == ESP_ERR_TIMEOUT);
    assert(display.faulted);
    stuck_busy = false;
    command_count = 0;
    assert(ssd1677_refresh(&display) == ESP_ERR_INVALID_STATE && command_count == 0);

    display = make_display();
    display.papermono_otp = true;
    display.set_reset = external_reset;
    display.reset_context = &fail_external_reset;
    display.reset_pin = -1;
    fail_external_reset = true;
    assert(ssd1677_refresh(&display) == ESP_FAIL);
    assert(command_count == 0 && display.faulted);
    fail_external_reset = false;
}

static void expect_fast(void)
{
    static const uint8_t sequence[] = {
        0x3c, 0x11, 0x44, 0x45, 0x4e, 0x4f, 0x24, 0x21, 0x22, 0x20, 0x10,
    };
    assert(command_count == sizeof(sequence));
    for (size_t i = 0; i < sizeof(sequence); i++) assert(commands[i].command == sequence[i]);
    expect_data(0x3c, (uint8_t[]){0x80}, 1);
    expect_data(0x11, (uint8_t[]){1}, 1);
    expect_data(0x44, (uint8_t[]){0, 0, 0x1f, 3}, 4);
    expect_data(0x45, (uint8_t[]){0xdf, 1, 0, 0}, 4);
    expect_data(0x4e, (uint8_t[]){0, 0}, 2);
    expect_data(0x4f, (uint8_t[]){0xdf, 1}, 2);
    expect_data(0x21, (uint8_t[]){0}, 1);
    expect_data(0x22, (uint8_t[]){0xff}, 1);
    expect_data(0x10, (uint8_t[]){1}, 1);
    assert(find_command(0x24, 0)->length == sizeof(frame));
}

static void test_papermono_fast_state(void)
{
    epd_ssd1677_t display = make_display();
    display.papermono_otp = true;
    assert(epd_ssd1677_set_controller_mode(&display, "refresh=partial") == ESP_OK);
    /* BOOT must establish a visible full baseline, never a RAM-only preload. */
    assert(ssd1677_refresh(&display) == ESP_OK);
    expect_data(0x22, (uint8_t[]){0xf8}, 1);
    assert(!display.timing.partial && display.timing.writes == 3 && display.timing.activations == 2);
    assert(epd_ssd1677_set_controller_mode(&display, "refresh=auto") == ESP_OK);
    for (unsigned n = 0; n < 25; n++) {
        command_count = 0;
        frame[0] ^= 1;
        display.timing_enabled = n == 0;
        spi_delay_us = n == 0 ? 17 : 0;
        assert(ssd1677_refresh(&display) == ESP_OK);
        assert(display.timing.spi_us[0] == (n == 0 ? SSD1677_HEIGHT * 17 : 0));
        if (n == 0) {
            assert(display.timing.convert_start_us[0] >= display.timing.write_start_us[0]);
            assert(display.timing.convert_end_us[0] <= display.timing.write_end_us[0]);
            assert(display.timing.write_end_us[0] - display.timing.write_start_us[0] >=
                   display.timing.spi_us[0] + display.timing.convert_us[0]);
        }
        expect_fast(); /* Does not inherit generic SSD1677's 20-frame cleanup. */
        assert(find_command(0x24, 0)->data[0] == ((n & 1) ? 0xff : 0x7f));
        assert(display.papermono_baseline_valid && display.shadow_valid && !display.controller_ready);
        assert(display.timing.partial && display.timing.writes == 1 && display.timing.activations == 1);
        assert(display.timing.end_us >= display.timing.ready_us[0] + 100000);
    }
    command_count = 0;
    assert(ssd1677_refresh(&display) == ESP_OK && command_count == 0);
    /* Manual cleanup is effective even when the framebuffer is identical. */
    assert(epd_ssd1677_set_controller_mode(&display, "refresh=cleanup") == ESP_OK);
    assert(ssd1677_refresh(&display) == ESP_OK);
    assert(!display.timing.partial && !display.cleanup_pending);
    expect_data(0x22, (uint8_t[]){0xf8}, 1);
    command_count = 0;
    assert(ssd1677_sleep(&display) == ESP_OK);
    assert(!display.powered && !display.papermono_baseline_valid && !display.shadow_valid);
    /* Changed while suspended: next frame is full even though host shadow exists. */
    frame[799] = 0;
    command_count = 0;
    assert(ssd1677_refresh(&display) == ESP_OK && !display.timing.partial);
    command_count = 0;
    assert(epd_ssd1677_resume(&display) == ESP_OK);
    assert(!display.papermono_baseline_valid && !display.shadow_valid);
    command_count = 0;
    assert(ssd1677_refresh(&display) == ESP_OK && !display.timing.partial);
    command_count = 0;
    display.papermono_baseline_valid = false;
    assert(ssd1677_refresh(&display) == ESP_OK && !display.timing.partial);
    assert(command_count > 0); /* Unknown RAM overrides identical-frame skip. */
    command_count = 0;
    display.powered = false;
    assert(ssd1677_refresh(&display) == ESP_OK && !display.timing.partial);
    command_count = 0;
    display.shadow = NULL;
    assert(ssd1677_refresh(&display) == ESP_OK && !display.timing.partial);
}

static void test_papermono_fast_faults(void)
{
    const int failures[] = {0x24, 0x22, 0x20, 0x10, 0x100, -1};
    for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); i++) {
        epd_ssd1677_t display = make_display();
        display.papermono_otp = true;
        assert(ssd1677_refresh(&display) == ESP_OK);
        command_count = 0;
        fail_command = failures[i];
        fail_plane_data = failures[i] == 0x100;
        busy_on_activation = failures[i] < 0;
        frame[0] ^= 1;
        assert(ssd1677_refresh(&display) == (busy_on_activation ? ESP_ERR_TIMEOUT : ESP_FAIL));
        assert(display.faulted && !display.papermono_baseline_valid && !display.shadow_valid);
        /* No full retry after hardware failure, even after BUSY recovers. */
        stuck_busy = false;
        busy_on_activation = false;
        fail_command = -1;
        fail_plane_data = false;
        command_count = 0;
        assert(ssd1677_refresh(&display) == ESP_ERR_INVALID_STATE && command_count == 0);
        assert(epd_ssd1677_resume(&display) == ESP_ERR_INVALID_STATE && command_count == 0);
    }
}

static void test_immutable_snapshot_source(void)
{
    epd_ssd1677_t display = make_display();
    display.papermono_otp = true;
    assert(ssd1677_refresh(&display) == ESP_OK);
    static uint8_t snapshot[SSD1677_BUFFER_BYTES];
    memset(snapshot, 0xaa, sizeof(snapshot));
    command_count = 0;
    mutate_render_buffer = true; /* simulate rendering during SPI execution */
    assert(epd_ssd1677_present_snapshot(&display, snapshot, sizeof(snapshot)) == ESP_OK);
    assert(display.snapshot == NULL && display.papermono_baseline_valid);
    assert(memcmp(shadow, snapshot, sizeof(snapshot)) == 0);
    for (size_t i=0;i<sizeof(frame);i++) assert(frame[i] == 0x55 && snapshot[i] == 0xaa);
    captured_command_t *plane = find_command(0x24, 0);
    for (size_t row=0;row<SSD1677_HEIGHT;row++)
        for (size_t x=0;x<SSD1677_PANEL_ROW_BYTES;x++)
            assert(plane->data[row*SSD1677_PANEL_ROW_BYTES+x] == (row%2 ? 0xff : 0));
    const size_t before = command_count;
    assert(epd_ssd1677_present_snapshot(&display, snapshot, sizeof(snapshot)-1) == ESP_ERR_INVALID_ARG);
    assert(command_count == before);
}

static void test_dirty_boundaries(void)
{
    epd_ssd1677_t d = make_display();
    memcpy(shadow, frame, sizeof(frame));
    ssd1677_window_t w;
    assert(!ssd1677_find_change_window(&d, &w));
    /* Every corner and both sides of a byte/tile boundary. */
    const unsigned points[][2] = {{0,0},{799,0},{0,479},{799,479},
                                  {7,7},{8,8},{15,15},{16,16}};
    for (unsigned i=0; i<sizeof(points)/sizeof(points[0]); ++i) {
        const unsigned x=points[i][0], y=points[i][1];
        const size_t at=(y/8)*800+x;
        frame[at]^=(uint8_t)(1U<<(y%8));
        assert(ssd1677_find_change_window(&d,&w));
        assert(w.x_start_byte==x/8 && w.x_end_byte==x/8);
        assert(w.y_start==y && w.y_end==y);
        frame[at]=shadow[at];
    }
    frame[800+33]^=0xff; /* one vertical framebuffer byte */
    assert(ssd1677_find_change_window(&d,&w));
    assert(w.x_start_byte==4 && w.x_end_byte==4 && w.y_start==8 && w.y_end==15);
    memcpy(frame,shadow,sizeof(frame));
    for (unsigned x=0; x<800; ++x) frame[1600+x]^=4;
    assert(ssd1677_find_change_window(&d,&w));
    assert(w.x_start_byte==0 && w.x_end_byte==99 && w.y_start==18 && w.y_end==18);
    memcpy(frame,shadow,sizeof(frame));
    frame[0]^=1; frame[sizeof(frame)-1]^=0x80;
    assert(ssd1677_find_change_window(&d,&w));
    assert(w.x_start_byte==0 && w.x_end_byte==99 && w.y_start==0 && w.y_end==479);
}

static void test_reference_lifecycle(void)
{
    epd_ssd1677_t d=make_display(); d.papermono_otp=true;
    assert(epd_ssd1677_set_controller_mode(&d,"refresh=fastest")==ESP_OK);
    assert(!strcmp(epd_ssd1677_controller_mode(&d),"refresh=fastest"));
    assert(ssd1677_refresh(&d)==ESP_OK);
    frame[0]^=1; command_count=0;
    assert(ssd1677_refresh(&d)==ESP_OK && d.reference_awake);
    /* Explicit cleanup remains effective with an identical snapshot. */
    assert(epd_ssd1677_set_controller_mode(&d,"refresh=cleanup")==ESP_OK);
    command_count=0;
    assert(ssd1677_refresh(&d)==ESP_OK && !d.timing.partial && !d.reference_awake);
    assert(d.papermono_baseline_valid && !memcmp(frame,shadow,sizeof(frame)));
    frame[1]^=1; command_count=0;
    assert(ssd1677_refresh(&d)==ESP_OK);
    expect_data(0x22,(uint8_t[]){0xcc},1);
    command_count=0;
    assert(ssd1677_sleep(&d)==ESP_OK && !d.papermono_baseline_valid && !d.reference_awake);
    expect_data(0x22,(uint8_t[]){0x03},1);
    assert(commands[0].command==0x22 && commands[1].command==0x20 && commands[2].command==0x10);
    command_count=0;
    assert(ssd1677_refresh(&d)==ESP_OK && !d.timing.partial);
    /* BUSY failure must preserve completed bytes and latch further attempts. */
    frame[2]^=1; command_count=0; busy_on_activation=true;
    assert(ssd1677_refresh(&d)==ESP_ERR_TIMEOUT);
    assert(d.faulted && !d.shadow_valid && !d.papermono_baseline_valid);
    assert(shadow[2]==0xff);
    const size_t count=command_count;
    assert(ssd1677_refresh(&d)==ESP_ERR_INVALID_STATE && command_count==count);
    stuck_busy=false;
}

static void test_reference_completed_snapshots(void)
{
    epd_ssd1677_t d=make_display(); d.papermono_otp=true;
    assert(epd_ssd1677_set_controller_mode(&d,"refresh=reference")==ESP_OK);
    assert(ssd1677_refresh(&d)==ESP_OK); /* A: white */
    static uint8_t b[SSD1677_BUFFER_BYTES], latest[SSD1677_BUFFER_BYTES];
    memcpy(b,frame,sizeof(b)); b[0]^=1;
    command_count=0;
    assert(epd_ssd1677_present_snapshot(&d,b,sizeof(b))==ESP_OK);
    /* C was rendered then replaced before execution. D restores B's pixel
     * and changes a distant one. Both old and new transitions must be sent. */
    frame[400]^=4;
    memset(latest,0xff,sizeof(latest)); latest[sizeof(latest)-1]^=0x80;
    command_count=0; mutate_render_buffer=true;
    assert(epd_ssd1677_present_snapshot(&d,latest,sizeof(latest))==ESP_OK);
    const captured_command_t *old=find_command(0x26,0), *now=find_command(0x24,0);
    assert(old->data[0]==0x7f && now->data[0]==0xff);
    assert(old->data[47999]==0xff && now->data[47999]==0xfe);
    assert(!memcmp(shadow,latest,sizeof(latest)) && d.snapshot==NULL);
}

static void test_reference_power_off_failure(void)
{
    for (unsigned failure=0; failure<2; ++failure) {
        epd_ssd1677_t d=make_display(); d.papermono_otp=true;
        assert(epd_ssd1677_set_controller_mode(&d,"refresh=fastest")==ESP_OK);
        assert(ssd1677_refresh(&d)==ESP_OK);
        frame[0]^=1; command_count=0;
        assert(ssd1677_refresh(&d)==ESP_OK);
        command_count=0;
        const int powers=power_count, resets=reset_count;
        if (failure==0) fail_command=0x22;
        else busy_on_activation=true;
        assert(ssd1677_sleep(&d)!=ESP_OK);
        assert(d.faulted && !d.papermono_baseline_valid && !d.shadow_valid);
        assert(power_count==powers && reset_count==resets);
        for (size_t i=0;i<command_count;i++) assert(commands[i].command!=0x10);
        stuck_busy=false;
    }
}

static void test_fastest_conversion_equivalence(void)
{
    uint8_t columns[9] = {0}, rows[800];
    for (unsigned col=0; col<8; ++col) for (unsigned value=0; value<256; ++value) {
        memset(columns,0,sizeof(columns)); columns[col+1]=(uint8_t)value;
        ssd1677_transpose_tile(columns+1,rows,100); /* deliberately unaligned */
        for (unsigned y=0;y<8;++y)
            assert(rows[y*100] == ((value&(1U<<y)) ? (0x80U>>col) : 0));
    }
    /* Every possible old/new byte transition, no classification changes. */
    for (unsigned old=0;old<256;++old) for (unsigned now=0;now<256;++now) {
        for (unsigned c=0;c<8;++c) columns[c+1]=(uint8_t)((c&1) ? old : now);
        ssd1677_transpose_tile(columns+1,rows,100);
        for (unsigned y=0;y<8;++y) {
            unsigned expected=0;
            for (unsigned c=0;c<8;++c) if (columns[c+1]&(1U<<y)) expected|=0x80U>>c;
            assert(rows[y*100]==expected);
        }
    }
    epd_ssd1677_t d=make_display(); d.papermono_otp=true; d.band_buffer=band;
    d.fastest_tiles=true; d.fastest_spi_hz=40000000;
    assert(epd_ssd1677_set_controller_mode(&d,"refresh=fastest")==ESP_OK);
    assert(ssd1677_refresh(&d)==ESP_OK);
    uint32_t random=1;
    for (size_t i=0;i<sizeof(frame);++i) {
        random=random*1664525U+1013904223U;
        frame[i]=(uint8_t)(random>>24);
    }
    command_count=0;
    assert(ssd1677_refresh(&d)==ESP_OK);
    assert(d.actual_spi_hz==40000000);
    const captured_command_t *current=find_command(0x24,0), *old=find_command(0x26,0);
    assert(current->length==sizeof(frame) && old->length==sizeof(frame));
    for (unsigned y=0;y<480;++y) {
        ssd1677_convert_row(&d,frame,y);
        assert(!memcmp(current->data+y*100,line,100));
        for (unsigned x=0;x<100;++x) assert(old->data[y*100+x]==0xff);
    }
    for (unsigned trial=0;trial<128;++trial) {
        memcpy(shadow,frame,sizeof(frame));
        if (trial) {
            frame[(trial*379U)%sizeof(frame)]^=(uint8_t)trial;
            frame[(trial*311U)%sizeof(frame)]^=0x80;
        }
        ssd1677_window_t scalar, bytes;
        assert(ssd1677_find_change_window(&d,&scalar)==ssd1677_find_change_bytes(&d,&bytes));
        assert(!memcmp(&scalar,&bytes,sizeof(scalar)));
    }
    static uint8_t completed[SSD1677_BUFFER_BYTES];
    for (unsigned pattern=0;pattern<5;++pattern) {
        memcpy(completed,shadow,sizeof(completed));
        for (size_t i=0;i<sizeof(frame);++i)
            frame[i]=pattern==0 ? 0xff : pattern==1 ? 0 :
                     pattern==2 ? ((i&1) ? 0xaa : 0x55) :
                     pattern==3 ? ((i&1) ? 0x55 : 0xaa) : 0xff;
        if (pattern==4) { frame[0]=0xfe; frame[sizeof(frame)-1]=0x7f; }
        command_count=0;
        assert(ssd1677_refresh(&d)==ESP_OK);
        current=find_command(0x24,0); old=find_command(0x26,0);
        assert(current->length==48000 && old->length==48000);
        for (unsigned y=0;y<480;++y) {
            ssd1677_convert_row(&d,frame,y);
            assert(!memcmp(current->data+y*100,line,100));
            ssd1677_convert_row(&d,completed,y);
            assert(!memcmp(old->data+y*100,line,100));
        }
        assert(!memcmp(frame,shadow,sizeof(frame)));
        command_count=0;
        assert(ssd1677_refresh(&d)==ESP_OK && command_count==0);
    }
    command_count=0; frame[0]^=1; fail_plane_data=true;
    const uint8_t old_first=shadow[0];
    assert(ssd1677_refresh(&d)!=ESP_OK && d.faulted && !d.shadow_valid);
    assert(shadow[0]==old_first);
    d=make_display(); d.papermono_otp=true; d.band_buffer=band;
    d.fastest_tiles=true; d.fastest_spi_hz=40000000; d.configured_spi_hz=10000000;
    assert(epd_ssd1677_set_controller_mode(&d,"refresh=fastest")==ESP_OK);
    assert(ssd1677_refresh(&d)==ESP_OK);
    frame[0]^=1; busy_on_activation=true;
    assert(ssd1677_refresh(&d)==ESP_ERR_TIMEOUT && d.faulted && !d.shadow_valid);
    assert(shadow[0]==0xff);
    stuck_busy=false; busy_on_activation=false;
    d=make_display(); d.papermono_otp=true; d.band_buffer=band;
    d.fastest_tiles=true; d.fastest_spi_hz=40000000; d.configured_spi_hz=10000000;
    assert(epd_ssd1677_set_controller_mode(&d,"refresh=fastest")==ESP_OK);
    assert(ssd1677_refresh(&d)==ESP_OK);
    frame[0]^=1; assert(ssd1677_refresh(&d)==ESP_OK && d.actual_spi_hz==40000000);
    assert(epd_ssd1677_set_controller_mode(&d,"refresh=auto")==ESP_OK);
    command_count=0;
    assert(ssd1677_refresh(&d)==ESP_OK && d.actual_spi_hz==10000000 && !d.timing.partial);
}

static void test_fastest_snapshot_ownership(void)
{
    epd_ssd1677_t d=make_display(); d.papermono_otp=true; d.band_buffer=band;
    d.fastest_tiles=true; d.fastest_spi_hz=40000000; d.configured_spi_hz=10000000;
    assert(epd_ssd1677_set_controller_mode(&d,"refresh=fastest")==ESP_OK);
    assert(ssd1677_refresh(&d)==ESP_OK);
    static uint8_t immutable[SSD1677_BUFFER_BYTES];
    memset(immutable,0x33,sizeof(immutable));
    memset(frame,0xaa,sizeof(frame));
    command_count=0; mutate_render_buffer=true;
    assert(epd_ssd1677_present_snapshot(&d,immutable,sizeof(immutable))==ESP_OK);
    assert(d.snapshot==NULL && !memcmp(shadow,immutable,sizeof(shadow)));
    assert(frame[0]==0x55 && immutable[0]==0x33);
    const captured_command_t *current=find_command(0x24,0), *old=find_command(0x26,0);
    assert(current->length==48000 && old->length==48000);
    for (unsigned y=0;y<480;++y) {
        ssd1677_convert_row(&d,immutable,y);
        assert(!memcmp(current->data+y*100,line,100));
        for (unsigned x=0;x<100;++x) assert(old->data[y*100+x]==0xff);
    }
}

static void test_reference_prototype(void)
{
    epd_ssd1677_t d = make_display();
    assert(epd_ssd1677_set_controller_mode(&d,"refresh=fastest") == ESP_ERR_INVALID_ARG);
    assert(epd_ssd1677_set_controller_mode(&d,"refresh=reference") == ESP_ERR_INVALID_ARG);
    d.papermono_otp=true;
    assert(epd_ssd1677_set_controller_mode(&d,"refresh=reference") == ESP_OK);
    assert(ssd1677_refresh(&d)==ESP_OK); /* visible OTP baseline */
    assert(!d.reference_awake && !d.timing.partial);
    frame[0]^=1;
    command_count=0;
    assert(ssd1677_refresh(&d)==ESP_OK && d.reference_awake);
    assert(find_command(0x24,0)->length==48000 && find_command(0x26,0)->length==48000);
    assert(find_command(0x24,0)->data[0]==0x7f && find_command(0x26,0)->data[0]==0xff);
    assert(find_command(0x32,0)->length==105);
    expect_data(0x22,(uint8_t[]){0xcc},1);
    assert(d.papermono_baseline_valid && !memcmp(frame,shadow,sizeof(frame)));
    command_count=0;
    assert(ssd1677_refresh(&d)==ESP_OK && !command_count);
    frame[0]^=1;
    assert(ssd1677_refresh(&d)==ESP_OK);
    expect_data(0x22,(uint8_t[]){0x0c},1);
    assert(find_command(0x26,0)->data[0]==0x7f);
    for (size_t i=0;i<command_count;i++) assert(commands[i].command!=0x12 && commands[i].command!=0x10);
    command_count=0;
    frame[0]^=1;
    fail_command=0x32;
    assert(ssd1677_refresh(&d)!=ESP_OK && d.faulted && !d.shadow_valid);
    assert(shadow[0]==0xff && !d.papermono_baseline_valid);
    d=make_display();d.papermono_otp=true;
    assert(epd_ssd1677_set_controller_mode(&d,"refresh=reference")==ESP_OK);
    assert(ssd1677_refresh(&d)==ESP_OK);
    assert(epd_ssd1677_set_controller_mode(&d,"refresh=auto")==ESP_OK);
    assert(!d.papermono_baseline_valid);
    command_count=0;
    assert(ssd1677_refresh(&d)==ESP_OK && !d.timing.partial);
}

static void test_balanced_endpoints_and_lifecycle(void)
{
    for (unsigned tile = 0; tile < 2; ++tile) {
        epd_ssd1677_t d = make_display();
        assert(epd_ssd1677_set_controller_mode(&d,"refresh=balanced") == ESP_ERR_INVALID_ARG);
        d.papermono_otp = true;
        d.fastest_tiles = tile != 0;
        d.band_buffer = band;
        d.fastest_spi_hz = 40000000;
        d.configured_spi_hz = 10000000;
        assert(epd_ssd1677_set_controller_mode(&d,"refresh=balanced") == ESP_OK);
        assert(!strcmp(epd_ssd1677_controller_mode(&d),"refresh=balanced"));
        assert(ssd1677_refresh(&d) == ESP_OK && !d.timing.partial);
        /* A black mark stays black while a distant new mark changes. Both
         * absolute planes must contain CURRENT endpoints, never old pixels. */
        frame[0] = 0;
        command_count = 0;
        assert(ssd1677_refresh(&d) == ESP_OK && d.reference_awake);
        expect_data(0x22,(uint8_t[]){0xcc},1);
        frame[sizeof(frame)-1] = 0;
        command_count = 0;
        assert(ssd1677_refresh(&d) == ESP_OK);
        expect_data(0x22,(uint8_t[]){0x0c},1);
        const captured_command_t *a = find_command(0x24,0), *b = find_command(0x26,0);
        assert(a && b && a->length == sizeof(frame) && b->length == sizeof(frame));
        assert(!memcmp(a->data,b->data,a->length));
        assert(a->data[0] == 0x80 && a->data[sizeof(frame)-1] == 0x01);
        assert(a->data[101] == 0); /* white selects group 0, black group 3 */
        expect_data(0x32,ssd1677_balanced_lut,105);
        expect_data(0x04,(uint8_t[]){0x41,0xa8,0x32},3);
        assert(!memcmp(shadow,frame,sizeof(frame)) && d.actual_spi_hz == 40000000);
        command_count = 0;
        assert(ssd1677_refresh(&d) == ESP_OK && !command_count);
        assert(epd_ssd1677_set_controller_mode(&d,"refresh=cleanup") == ESP_OK);
        assert(ssd1677_refresh(&d) == ESP_OK && !d.timing.partial);
        assert(!strcmp(epd_ssd1677_controller_mode(&d),"refresh=balanced"));
        frame[1] = 0; command_count = 0;
        assert(ssd1677_refresh(&d) == ESP_OK);
        command_count = 0;
        assert(ssd1677_sleep(&d) == ESP_OK);
        assert(commands[0].command == 0x22 && commands[1].command == 0x20 &&
               commands[2].command == 0x10);
        assert(ssd1677_refresh(&d) == ESP_OK && !d.timing.partial);
        assert(epd_ssd1677_set_controller_mode(&d,"refresh=fastest") == ESP_OK);
        command_count = 0;
        assert(!d.papermono_baseline_valid && ssd1677_refresh(&d) == ESP_OK);
        assert(!d.timing.partial);
        for (size_t i=0; i<command_count; ++i) assert(commands[i].command != 0x32);
        assert(epd_ssd1677_set_controller_mode(&d,"refresh=balanced") == ESP_OK);
        command_count = 0;
        assert(!d.papermono_baseline_valid && ssd1677_refresh(&d) == ESP_OK);
        assert(!d.timing.partial);
        for (size_t i=0; i<command_count; ++i) assert(commands[i].command != 0x32);
    }
}

static void test_balanced_snapshot_and_faults(void)
{
    for (unsigned failure = 0; failure < 3; ++failure) {
        epd_ssd1677_t d = make_display(); d.papermono_otp = true;
        d.fastest_tiles = true; d.band_buffer = band;
        assert(epd_ssd1677_set_controller_mode(&d,"refresh=balanced") == ESP_OK);
        assert(ssd1677_refresh(&d) == ESP_OK);
        uint8_t latest[SSD1677_BUFFER_BYTES]; memcpy(latest,frame,sizeof(latest));
        latest[0] = 0;
        command_count = 0;
        if (failure == 0) {
            mutate_render_buffer = true;
            assert(epd_ssd1677_present_snapshot(&d,latest,sizeof(latest)) == ESP_OK);
            assert(!memcmp(shadow,latest,sizeof(latest)) && d.snapshot == NULL);
            assert(!memcmp(find_command(0x24,0)->data,find_command(0x26,0)->data,sizeof(latest)));
        } else {
            if (failure == 1) fail_plane_data = true;
            else busy_on_activation = true;
            assert(epd_ssd1677_present_snapshot(&d,latest,sizeof(latest)) != ESP_OK);
            assert(d.faulted && !d.shadow_valid && !d.papermono_baseline_valid);
            assert(shadow[0] == 0xff && d.snapshot == NULL);
            const size_t count = command_count;
            assert(ssd1677_refresh(&d) == ESP_ERR_INVALID_STATE && command_count == count);
        }
    }
    stuck_busy = false;
}

int main(void)
{
    test_initial_full();
    test_partial_and_cleanup();
    test_failure_and_no_shadow();
    test_papermono_otp();
    test_papermono_fast_state();
    test_papermono_fast_faults();
    test_immutable_snapshot_source();
    test_dirty_boundaries();
    test_reference_prototype();
    test_reference_lifecycle();
    test_reference_completed_snapshots();
    test_reference_power_off_failure();
    test_fastest_conversion_equivalence();
    test_fastest_snapshot_ownership();
    test_balanced_endpoints_and_lifecycle();
    test_balanced_snapshot_and_faults();
    puts("epd_ssd1677_test: PASS");
    return 0;
}
