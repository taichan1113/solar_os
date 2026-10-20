/* USB-only display-wide control. Legacy writer-lut is a compatibility alias. */
static const char *writer_lut_ram_path(epd_ssd1677_ram_path_t path)
{
    return path == SSD1677_RAM_FULL ? "full" : path == SSD1677_RAM_ROWS ? "rows" :
           path == SSD1677_RAM_RECT ? "rect" : path == SSD1677_RAM_OTP ? "otp" : "none";
}

static void writer_lut_metric(solar_os_shell_io_t *io, const char *name,
                               const epd_ssd1677_trial_metric_t *m)
{
    solar_os_shell_io_printf(io, "trial %s samples=%llu sum_us=%llu min_us=%llu max_us=%llu avg_us=%llu\n",
        name, (unsigned long long)m->count, (unsigned long long)m->sum_us,
        (unsigned long long)m->min_us, (unsigned long long)m->max_us,
        (unsigned long long)(m->count ? m->sum_us / m->count : 0));
}

static void writer_lut_summary(solar_os_shell_io_t *io, const epd_ssd1677_trial_summary_t *s)
{
    solar_os_shell_io_printf(io,
        "trial started=%u active=%u start_us=%lld end_us=%lld elapsed_us=%lld seq=%llu,%llu frames=%llu physical=%llu A=%llu D=%llu E1=%llu OTP=%llu noop=%llu error=%llu overflow=%u\n",
        s->started, s->active, (long long)s->start_us, (long long)s->end_us,
        (long long)(s->end_us - s->start_us), (unsigned long long)s->first_seq,
        (unsigned long long)s->last_seq, (unsigned long long)s->frames,
        (unsigned long long)s->physical, (unsigned long long)s->a, (unsigned long long)s->d,
        (unsigned long long)s->e1, (unsigned long long)s->otp,
        (unsigned long long)s->noop, (unsigned long long)s->errors, s->overflow);
    solar_os_shell_io_printf(io,
        "trial source writer=%llu sketch=%llu other=%llu ram_bytes=%llu full=%llu rows=%llu rect=%llu otp_ram=%llu periodic_resync=%llu timing_missing=%llu\n",
        (unsigned long long)s->writer, (unsigned long long)s->sketch, (unsigned long long)s->other,
        (unsigned long long)s->ram_bytes, (unsigned long long)s->full_ram,
        (unsigned long long)s->rows_ram, (unsigned long long)s->rect_ram, (unsigned long long)s->otp_ram,
        (unsigned long long)s->periodic_resync, (unsigned long long)s->timing_missing);
    writer_lut_metric(io, "busy", &s->busy);
    writer_lut_metric(io, "activation_start_to_start", &s->start_gap);
    writer_lut_metric(io, "busy_ready_to_next_start", &s->ready_gap);
    solar_os_shell_io_printf(io,
        "trial queue valid=%u wrapped=%u submitted=%llu replaced=%llu cancelled=%llu rejected=%llu writer_coalesced=%llu pending_start=%u pending_end=%u\n",
        s->queue_valid, s->queue_wrapped, (unsigned long long)s->submitted,
        (unsigned long long)s->replaced, (unsigned long long)s->cancelled,
        (unsigned long long)s->rejected, (unsigned long long)s->writer_coalesced,
        s->pending_start, s->pending_end);
}

static void display_cmd_writer_lut(solar_os_shell_io_t *io, int argc, char **argv)
{
    if (argc != 5 || strcmp(argv[2], "display0") ||
        solar_os_shell_io_kind(io) != SOLAR_OS_SHELL_IO_KIND_PORT) {
        solar_os_shell_io_writeln(io,
            "USB: display test display0 fastest-lut status|baseline|candidate_d|candidate_e1|candidate_d_sketch|disable|trace|trial-start|trial-status|trial-stop (writer-lut alias)");
        return;
    }
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
    solar_os_shell_io_printf(io,
        "fastest-lut err=%d mode=%s scope=%s attempts=%llu D0x20=%llu success=%llu error=%llu driver_error=%llu failed=%u E1_attempts=%llu E10x20=%llu E1_success=%llu E1_error=%llu sketch_d_trial=%u\n",
        (int)err, s.enabled ? (s.candidate_e1 ? "E1" : "D") : "A",
        s.enabled && !s.candidate_e1 && !s.candidate_d_sketch ? "all-apps-except-sketch" : "all-apps",
        (unsigned long long)s.attempts, (unsigned long long)s.d_physical,
        (unsigned long long)s.d_success, (unsigned long long)s.d_errors,
        (unsigned long long)s.driver_errors, s.failed,
        (unsigned long long)s.e1_attempts, (unsigned long long)s.e1_physical,
        (unsigned long long)s.e1_success, (unsigned long long)s.e1_errors, s.candidate_d_sketch);
    if (!strncmp(argv[4], "trial-", 6)) writer_lut_summary(io, &s.trial);
    uint64_t total = 0;
    const uint64_t counts[] = {s.a_physical, s.d_physical, s.e1_physical, s.otp_physical};
    for (unsigned i = 0; i < sizeof(counts)/sizeof(counts[0]); i++)
        total = counts[i] > UINT64_MAX - total ? UINT64_MAX : total + counts[i];
    solar_os_shell_io_printf(io,
        "display physical=%llu A=%llu D=%llu OTP=%llu noop=%llu E1=%llu\n",
        (unsigned long long)total,
        (unsigned long long)s.a_physical, (unsigned long long)s.d_physical,
        (unsigned long long)s.otp_physical, (unsigned long long)s.noop, (unsigned long long)s.e1_physical);
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
                r->candidate_e1 ? "E1" : r->candidate_d ? "D" : r->reference ? "A" : "OTP/noop", (unsigned)r->physical,
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
            solar_os_shell_io_printf(io,
                "WL diag seq=%llu activation_seq=%llu,%llu ram_path=%s ram_window=%u,%u,%u,%u periodic_resync=%u busy_us=%lld start_gap_us=%lld ready_gap_us=%lld timing_valid=%u\n",
                (unsigned long long)r->seq, (unsigned long long)r->activation_first_seq,
                (unsigned long long)r->activation_last_seq, writer_lut_ram_path(r->ram_path),
                r->ram_x, r->ram_y, r->ram_width, r->ram_height, r->periodic_resync,
                (long long)r->busy_us, (long long)r->start_gap_us, (long long)r->ready_gap_us, r->timing_valid);
        }
        free(records);
    }
}
