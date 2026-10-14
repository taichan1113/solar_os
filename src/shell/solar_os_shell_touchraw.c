#include "solar_os_shell_commands.h"
#include "solar_os_shell_common.h"
#include "solar_os_ft6336_raw.h"
#include "solar_os_cdc.h"
#include "solar_os_port_shell.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static esp_err_t print_status(solar_os_shell_io_t *io, const solar_os_touchraw_status_t *s)
{
    return solar_os_shell_io_printf(io,
        "TOUCHRAW status mode=%u end=%u reads=%" PRIu32 " stored=%" PRIu32
        " dropped=%" PRIu32 " failures=%" PRIu32 " max_consecutive=%" PRIu32
        " capacity=%u record_bytes=%u logger_total_us=%" PRIu64 " logger_max_us=%" PRIu32
        " results=%" PRIu32 ",%" PRIu32 ",%" PRIu32
        " counts=%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32
        " flags=%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32
        " reported_flags=%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32
        " sentinel=%" PRIu32 " reported_sentinel=%" PRIu32 " duplicates=%" PRIu32 "\r\n",
        (unsigned)s->mode, (unsigned)s->end, s->reads, s->stored, s->dropped,
        s->failures, s->max_consecutive, SOLAR_OS_TOUCHRAW_CAPACITY,
        (unsigned)sizeof(solar_os_touchraw_record_t), s->logger_total_us, s->logger_max_us,
        s->results[0], s->results[1], s->results[2],
        s->counts[0], s->counts[1], s->counts[2], s->counts[3],
        s->flags[0], s->flags[1], s->flags[2], s->flags[3],
        s->reported_flags[0], s->reported_flags[1], s->reported_flags[2], s->reported_flags[3],
        s->sentinel_slots, s->reported_sentinel, s->duplicates);
}

static void dump(solar_os_shell_io_t *io)
{
    solar_os_touchraw_status_t status;
    esp_err_t err = solar_os_ft6336_raw_dump_begin(&status);
    if (err != ESP_OK) {
        solar_os_shell_io_writeln(io, "touchraw: stop acquisition before dump");
        return;
    }
    err = print_status(io, &status);
    int64_t previous = 0;
    uint32_t emitted = 0;
    for (size_t i = 0; err == ESP_OK && i < status.stored; ++i) {
        solar_os_touchraw_record_t r;
        err = solar_os_ft6336_raw_record(i, &r);
        if (err != ESP_OK) break;
        char hex[FT6336_FRAME_BYTES * 2U + 1U];
        for (size_t j = 0; j < sizeof(r.raw); j++)
            snprintf(hex + j * 2U, 3U, "%02x", r.raw[j]);
        ft6336_frame_t f = {0};
        if (r.error == ESP_OK) ft6336_frame_decode(r.raw, sizeof(r.raw), &f);
        err = solar_os_shell_io_printf(io,
            "TOUCHRAW sample seq=%" PRIu32 " start=%" PRId64 " end=%" PRId64
            " duration=%" PRId64 " interval=%" PRId64 " error=%" PRId32
            " measured=%u raw=%s result=%u reasons=%" PRIu32
            " p1=%u,%u,%u,%u,%u,%u p2=%u,%u,%u,%u,%u,%u\r\n",
            r.sequence, r.start_us, r.end_us, r.end_us - r.start_us,
            previous ? r.start_us - previous : 0, r.error, r.error == ESP_OK ? 1U : 0U,
            hex, (unsigned)r.result, r.reasons,
            f.contacts[0].id, f.contacts[0].x, f.contacts[0].y, (unsigned)f.contacts[0].event,
            f.contacts[0].reported, f.contacts[0].valid,
            f.contacts[1].id, f.contacts[1].x, f.contacts[1].y, (unsigned)f.contacts[1].event,
            f.contacts[1].reported, f.contacts[1].valid);
        previous = r.start_us;
        if (err == ESP_OK) emitted++;
    }
    solar_os_ft6336_raw_dump_end();
    if (err == ESP_OK) solar_os_shell_io_printf(io,
        "TOUCHRAW end emitted=%" PRIu32 " stored=%" PRIu32 " dropped=%" PRIu32 "\r\n",
        emitted, status.stored, status.dropped);
}

void solar_os_shell_cmd_touchraw(solar_os_context_t *ctx, int argc, char **argv)
{
    solar_os_shell_io_t *io = solar_os_shell_command_io(ctx);
    solar_os_port_info_t port;
    unsigned session_id = 0;
    /* No EPD diagnostic output or serial transport ambiguity. */
    if (!io || io->kind != SOLAR_OS_SHELL_IO_KIND_PORT ||
        solar_os_port_get_info(SOLAR_OS_CDC_PORT_NAME, &port) != ESP_OK ||
        !port.claimed || !solar_os_port_handle_valid(&io->port) ||
        sscanf(port.owner, "session:%u", &session_id) != 1 || session_id > UINT8_MAX ||
        !solar_os_port_shell_context_owns_session(ctx, (uint8_t)session_id)) {
        solar_os_shell_io_writeln(io, "touchraw: use USB Shell (cdc0)");
        return;
    }
    if (argc != 2) {
        solar_os_shell_io_writeln(io, "touchraw <start|status|stop|dump|clear>");
        return;
    }
    esp_err_t err = ESP_OK;
    if (!strcmp(argv[1], "start")) err = solar_os_ft6336_raw_start(&io->port);
    else if (!strcmp(argv[1], "stop")) err = solar_os_ft6336_raw_stop();
    else if (!strcmp(argv[1], "clear")) err = solar_os_ft6336_raw_clear();
    else if (!strcmp(argv[1], "dump")) { dump(io); return; }
    else if (strcmp(argv[1], "status")) {
        solar_os_shell_io_writeln(io, "touchraw <start|status|stop|dump|clear>");
        return;
    }
    if (err != ESP_OK) solar_os_shell_io_printf(io, "touchraw: %s\r\n", esp_err_to_name(err));
    solar_os_touchraw_status_t status;
    solar_os_ft6336_raw_status(&status);
    print_status(io, &status);
}
