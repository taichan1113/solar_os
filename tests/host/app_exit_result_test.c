#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "solar_os.h"
#include "solar_os_shell_io.h"
#include "freertos/task.h"
#include "solar_os_log.h"

esp_err_t solar_os_log_write(solar_os_log_level_t level, const char *tag,
                            const char *fmt, ...)
{
    (void)level; (void)tag; (void)fmt;
    return ESP_OK;
}

static bool on_application_task;
static unsigned queued_reboots, direct_reboots;
bool solar_os_sessions_is_application_task(void) { return on_application_task; }
esp_err_t solar_os_sessions_reboot(solar_os_context_t *ctx, const char *status)
{
    (void)ctx;
    assert(strcmp(status, "test reboot") == 0);
    queued_reboots++;
    return ESP_FAIL; /* Admission failure must never bypass the main-task join. */
}
void esp_restart(void) { direct_reboots++; }
void vTaskDelay(TickType_t ticks) { (void)ticks; }
void solar_os_splash_draw_reboot(solar_os_gfx_t *gfx, const char *status)
{
    (void)gfx; (void)status;
}

typedef struct {
    char text[32];
    size_t len;
} output_capture_t;

static esp_err_t capture_output(const char *text, size_t len, void *user)
{
    output_capture_t *capture = user;
    if (capture == NULL || text == NULL || capture->len + len >= sizeof(capture->text)) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(&capture->text[capture->len], text, len);
    capture->len += len;
    capture->text[capture->len] = '\0';
    return ESP_OK;
}

void solar_os_terminal_put_char(solar_os_terminal_t *terminal, char ch)
{
    (void)terminal;
    (void)ch;
}

void solar_os_terminal_clear(solar_os_terminal_t *terminal)
{
    (void)terminal;
}

size_t solar_os_terminal_cursor_row(const solar_os_terminal_t *terminal)
{
    (void)terminal;
    return 0U;
}

size_t solar_os_terminal_cursor_col(const solar_os_terminal_t *terminal)
{
    (void)terminal;
    return 0U;
}

bool solar_os_port_handle_valid(const solar_os_port_handle_t *handle)
{
    (void)handle;
    return false;
}

esp_err_t solar_os_port_write(const solar_os_port_handle_t *handle,
                              const uint8_t *data,
                              size_t len,
                              size_t *written)
{
    (void)handle;
    (void)data;
    if (written != NULL) {
        *written = len;
    }
    return ESP_OK;
}

int main(void)
{
    solar_os_context_t ctx;
    solar_os_context_init(&ctx, NULL, NULL);
    assert(solar_os_context_app_class(&ctx) ==
           SOLAR_OS_APP_CLASS_UNSPECIFIED);

    output_capture_t capture = {0};
    solar_os_context_set_output_handler(&ctx, capture_output, &capture);
    solar_os_shell_io_t io = {
        .kind = SOLAR_OS_SHELL_IO_KIND_TERMINAL,
        .terminal = (solar_os_terminal_t *)(uintptr_t)1U,
    };
    solar_os_context_set_shell_io(&ctx, &io);
    solar_os_context_set_app_class(&ctx, SOLAR_OS_APP_CLASS_COMMAND);
    assert(solar_os_shell_io_write(&io, "hello") == ESP_OK);
    assert(solar_os_shell_io_clear(&io) == ESP_OK);
    assert(solar_os_shell_io_write(&io, " world") == ESP_OK);
    assert(strcmp(capture.text, "hello world") == 0);

    solar_os_context_set_app_class(&ctx, SOLAR_OS_APP_CLASS_TUI);
    assert(solar_os_shell_io_write(&io, " hidden screen") == ESP_OK);
    assert(strcmp(capture.text, "hello world") == 0);

    solar_os_context_finish(&ctx, 7, "could not load");
    solar_os_context_finish(&ctx, 0, "overwritten");
    assert(solar_os_context_take_exit_request(&ctx));
    assert(!solar_os_context_take_terminal_preserve(&ctx));
    int exit_code = 0;
    assert(solar_os_context_take_exit_result(&ctx, &exit_code));
    assert(exit_code == 7);
    char message[SOLAR_OS_CONTEXT_STATUS_MESSAGE_MAX];
    assert(solar_os_context_take_status_message(&ctx, message, sizeof(message)));
    assert(strcmp(message, "could not load") == 0);

    solar_os_context_set_app_class(&ctx, SOLAR_OS_APP_CLASS_COMMAND);
    solar_os_context_request_deep_sleep(&ctx);
    assert(solar_os_context_take_deep_sleep_request(&ctx));
    assert(!solar_os_context_take_deep_sleep_request(&ctx));
    solar_os_context_finish(&ctx, 0, NULL);
    assert(solar_os_context_take_exit_request(&ctx));
    assert(solar_os_context_take_terminal_preserve(&ctx));
    assert(solar_os_context_take_exit_result(&ctx, &exit_code));
    assert(exit_code == 0);

    puts("app exit result tests passed");
    solar_os_context_reboot(NULL, "test reboot");
    assert(queued_reboots == 1 && direct_reboots == 0);
    on_application_task = true;
    solar_os_context_reboot(NULL, "test reboot");
    assert(queued_reboots == 1 && direct_reboots == 1);
    return 0;
}
