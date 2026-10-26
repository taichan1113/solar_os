/* Local/USB display-wide recovery and diagnostics. Keep writer-lut as an alias. */
static void display_print_fastest_lut_usage(solar_os_shell_io_t *io)
{
#if defined(SOLAR_OS_BOARD_M5PAPER_MONO) && SOLAR_OS_BOARD_M5PAPER_MONO
    solar_os_shell_io_writeln(io,
        "  display test display0 fastest-lut status|baseline|candidate_f|disable|trace (screen/USB)");
    solar_os_shell_io_writeln(io,
        "    F defaults to all apps; baseline/disable selects A globally; candidate_f restores F");
#else
    solar_os_shell_io_writeln(io,
        "  display test display0 fastest-lut status|baseline|candidate_d|candidate_f|disable|trace (screen/USB)");
    solar_os_shell_io_writeln(io,
        "    D excludes Sketch; F applies to all apps; baseline/disable selects A globally");
#endif
}

static void display_cmd_writer_lut(solar_os_shell_io_t *io, int argc, char **argv)
{
    if (argc != 5 || strcmp(argv[2], "display0") ||
        (solar_os_shell_io_kind(io) != SOLAR_OS_SHELL_IO_KIND_PORT &&
         solar_os_shell_io_kind(io) != SOLAR_OS_SHELL_IO_KIND_TERMINAL)) {
        display_print_fastest_lut_usage(io);
        return;
    }
    if (!strcmp(argv[3], "writer-lut"))
        solar_os_shell_io_writeln(io, "writer-lut: legacy alias; use fastest-lut");
    solar_os_display_target_t target;
    if (!solar_os_display_find_target(argv[2], &target) || !target.ready ||
        !target.u8g2 || !target.driver || strcmp(target.driver, "ssd1677")) {
        solar_os_shell_io_writeln(io, "fastest-lut: ready SSD1677 display0 required");
        return;
    }
    epd_ssd1677_t *d = (epd_ssd1677_t *)((uint8_t *)target.u8g2 - offsetof(epd_ssd1677_t, u8g2));
    epd_ssd1677_writer_record_t *records = NULL;
    if (!strcmp(argv[4], "trace")) {
        records = calloc(SSD1677_WRITER_D_RECORDS, sizeof(*records));
        if (!records) { solar_os_shell_io_writeln(io, "fastest-lut: no trace memory"); return; }
    }
    epd_ssd1677_writer_status_t s;
    const esp_err_t err = epd_ssd1677_writer_lut(d, argv[4], &s, records);
    if (d->default_f && !strcmp(argv[4], "candidate_d"))
        solar_os_shell_io_writeln(io,
            "fastest-lut: candidate_d unavailable with default F; use baseline or candidate_f");
    solar_os_shell_io_printf(io,
        "fastest-lut err=%d mode=%s scope=%s attempts=%llu D0x20=%llu success=%llu error=%llu driver_error=%llu failed=%u\n",
        (int)err, s.enabled ? (s.candidate_f ? "F" : "D") : "A",
        !s.enabled || s.candidate_f ? "all-apps" : "all-apps-except-sketch", (unsigned long long)s.attempts, (unsigned long long)s.d_physical,
        (unsigned long long)s.d_success, (unsigned long long)s.d_errors,
        (unsigned long long)s.driver_errors, s.failed);
    solar_os_shell_io_printf(io, "F attempts=%llu physical=%llu success=%llu error=%llu\n",
        (unsigned long long)s.f_attempts, (unsigned long long)s.f_physical,
        (unsigned long long)s.f_success, (unsigned long long)s.f_errors);
    uint64_t total = 0;
    const uint64_t counts[] = {s.a_physical,s.d_physical,s.f_physical,s.otp_physical};
    for (unsigned i=0;i<sizeof(counts)/sizeof(counts[0]);++i)
        total=counts[i]>UINT64_MAX-total ? UINT64_MAX : total+counts[i];
    solar_os_shell_io_printf(io,
        "display physical=%llu A=%llu D=%llu F=%llu OTP=%llu noop=%llu\n",
        (unsigned long long)total,
        (unsigned long long)s.a_physical, (unsigned long long)s.d_physical,
        (unsigned long long)s.f_physical,
        (unsigned long long)s.otp_physical, (unsigned long long)s.noop);
    solar_os_shell_io_printf(io,
        "writer physical=%llu A=%llu OTP=%llu noop=%llu other0x20=%llu power0x20=%llu records=%llu\n",
        (unsigned long long)s.writer_physical, (unsigned long long)s.writer_a,
        (unsigned long long)s.writer_otp, (unsigned long long)s.writer_noop, (unsigned long long)s.other_physical,
        (unsigned long long)s.power_controls, (unsigned long long)s.records);
    solar_os_display_submission_status_t queue;
    if (solar_os_display_submission_status(target.u8g2, &queue) == ESP_OK)
        solar_os_shell_io_printf(io,
            "queue submitted=%u replaced=%u writer=%u coalesced=%u cancelled=%u superseded=%u error=%d\n",
            (unsigned)queue.submitted, (unsigned)queue.replaced, (unsigned)queue.writer_submitted,
            (unsigned)queue.writer_coalesced, (unsigned)queue.writer_cancelled,
            (unsigned)queue.writer_superseded, (int)queue.error);
    if (records) {
        const unsigned first = (s.trace_next + SSD1677_WRITER_D_RECORDS - s.trace_count) % SSD1677_WRITER_D_RECORDS;
        for (unsigned i = 0; i < s.trace_count; i++) {
            const epd_ssd1677_writer_record_t *r = &records[(first + i) % SSD1677_WRITER_D_RECORDS];
            solar_os_shell_io_printf(io,
                "WL seq=%llu gen=%llu source=%s mode=%s 0x20=%u err=%d restored=%u input=%u merged=%u ram=%u region=%u,%u,%u,%u\n",
                (unsigned long long)r->seq, (unsigned long long)r->origin.generation,
                r->origin.is_sketch ? "sketch" : r->origin.is_writer ? "writer" : "other",
                r->candidate_f ? "F" : r->candidate_d ? "D" : r->reference ? "A" : "OTP/noop", (unsigned)r->physical,
                (int)r->error, r->restored, (unsigned)r->origin.inputs,
                (unsigned)r->origin.coalesced, (unsigned)r->ram_bytes, r->x, r->y, r->width, r->height);
            solar_os_shell_io_printf(io,
                "WL time seq=%llu input=%lld,%lld render=%lld,%lld queue=%lld,%lld execute=%lld,%lld\n",
                (unsigned long long)r->seq, (long long)r->origin.input_first_us, (long long)r->origin.input_last_us,
                (long long)r->origin.render_start_us, (long long)r->origin.rendered_us,
                (long long)r->origin.queued_first_us, (long long)r->origin.queued_us,
                (long long)r->start_us, (long long)r->end_us);
            solar_os_shell_io_printf(io,
                "WL panel seq=%llu ram=%lld,%lld activate=%lld ready=%lld spi_us=%lld lut_us=%lld restore_us=%lld\n",
                (unsigned long long)r->seq, (long long)r->ram_start_us, (long long)r->ram_end_us,
                (long long)r->activation_us, (long long)r->ready_us, (long long)r->spi_us,
                (long long)r->lut_us, (long long)r->restore_us);
        }
        free(records);
    }
}
