/* Fixed-size, RAM-only interval statistics. Called by the serialized Driver
 * worker; controls use its existing barrier. No per-frame allocation/log/I/O. */
static void writer_trial_add(epd_ssd1677_trial_summary_t *s, uint64_t *value, uint64_t n)
{
    if (n > UINT64_MAX - *value) { *value = UINT64_MAX; s->overflow = true; }
    else *value += n;
}

static void writer_trial_metric(epd_ssd1677_trial_summary_t *s,
                                epd_ssd1677_trial_metric_t *m, uint64_t us)
{
    if (!m->count || us < m->min_us) m->min_us = us;
    if (us > m->max_us) m->max_us = us;
    writer_trial_add(s, &m->count, 1);
    writer_trial_add(s, &m->sum_us, us);
}

static void writer_trial_finish(epd_ssd1677_t *d, epd_ssd1677_writer_record_t *r)
{
    epd_ssd1677_writer_trial_t *t = &d->writer_trial;
    epd_ssd1677_trial_summary_t *s = &t->status.trial;
    r->start_gap_us = r->ready_gap_us = -1;
    if (s->active) {
        if (!s->frames) s->first_seq = r->seq;
        s->last_seq = r->seq;
        writer_trial_add(s, &s->frames, 1);
        writer_trial_add(s, &s->physical, r->physical);
        writer_trial_add(s, r->candidate_e1 ? &s->e1 : r->candidate_d ? &s->d :
                            r->reference ? &s->a : &s->otp, r->physical);
        writer_trial_add(s, r->origin.is_sketch ? &s->sketch :
                            r->origin.is_writer ? &s->writer : &s->other, r->physical);
        if (r->error != ESP_OK) writer_trial_add(s, &s->errors, 1);
        if (!r->physical && r->error == ESP_OK) writer_trial_add(s, &s->noop, 1);
        writer_trial_add(s, &s->ram_bytes, r->ram_bytes);
        if (r->ram_bytes) {
            uint64_t *path = r->ram_path == SSD1677_RAM_FULL ? &s->full_ram :
                r->ram_path == SSD1677_RAM_ROWS ? &s->rows_ram :
                r->ram_path == SSD1677_RAM_RECT ? &s->rect_ram : &s->otp_ram;
            writer_trial_add(s, path, 1);
            if (r->periodic_resync) writer_trial_add(s, &s->periodic_resync, 1);
        }
    }
    /* Existing monotonic timestamps bracket the actual command and BUSY wait.
     * OTP full has two image activations. Power-only 0x20 is excluded. */
    const unsigned n = r->timing_valid ?
        (r->physical < d->timing.activations ? r->physical : d->timing.activations) : 0;
    if (s->active && r->physical > n) writer_trial_add(s, &s->timing_missing, r->physical - n);
    for (unsigned i = 0; i < n; i++) {
        const int64_t start = d->timing.activate_us[i], ready = d->timing.ready_us[i];
        if (!i) {
            if (t->last_activation_valid && start >= t->last_activation_us)
                r->start_gap_us = start - t->last_activation_us;
            if (t->last_ready_valid && start >= t->last_ready_us)
                r->ready_gap_us = start - t->last_ready_us;
        }
        const bool busy_valid = r->error == ESP_OK && ready >= start;
        if (busy_valid) r->busy_us += ready - start;
        if (s->active) {
            if (busy_valid) writer_trial_metric(s, &s->busy, (uint64_t)(ready - start));
            else writer_trial_add(s, &s->timing_missing, 1);
            if (t->trial_last_activation_valid && start >= t->trial_last_activation_us)
                writer_trial_metric(s, &s->start_gap, (uint64_t)(start - t->trial_last_activation_us));
            if (t->trial_last_ready_valid && start >= t->trial_last_ready_us)
                writer_trial_metric(s, &s->ready_gap, (uint64_t)(start - t->trial_last_ready_us));
            t->trial_last_activation_us = start;
            t->trial_last_activation_valid = true;
            t->trial_last_ready_us = ready;
            t->trial_last_ready_valid = busy_valid;
        }
        t->last_activation_us = start;
        t->last_activation_valid = true;
        t->last_ready_us = ready;
        t->last_ready_valid = busy_valid;
    }
    if (r->physical && !n) {
        t->last_activation_valid = t->last_ready_valid = false;
        if (s->active) t->trial_last_activation_valid = t->trial_last_ready_valid = false;
    }
}

static void writer_trial_queue(epd_ssd1677_t *d, bool start)
{
    epd_ssd1677_writer_trial_t *t = &d->writer_trial;
    epd_ssd1677_trial_summary_t *s = &t->status.trial;
    solar_os_display_submission_status_t q;
    if (!d->submission_status || d->submission_status(d->execution_context, &q) != ESP_OK) {
        s->queue_valid = false;
        return;
    }
    if (start) {
        t->trial_queue_start = q;
        s->queue_valid = true;
        s->pending_start = q.pending;
    } else if (s->queue_valid) {
        /* Existing uint32 Executor counters are not reset. Flag any observed
         * wrap; the modular deltas are not reliable after multiple wraps. */
        const uint32_t now[] = {q.submitted, q.replaced, q.cancelled, q.rejected, q.writer_coalesced};
        const solar_os_display_submission_status_t *b = &t->trial_queue_start;
        const uint32_t old[] = {b->submitted, b->replaced, b->cancelled, b->rejected, b->writer_coalesced};
        uint64_t *out[] = {&s->submitted, &s->replaced, &s->cancelled, &s->rejected, &s->writer_coalesced};
        for (unsigned i = 0; i < 5; i++) {
            if (now[i] < old[i]) s->queue_wrapped = true;
            *out[i] = (uint32_t)(now[i] - old[i]);
        }
        s->pending_end = q.pending;
    }
}

static esp_err_t writer_trial_control(epd_ssd1677_t *d, const char *mode)
{
    epd_ssd1677_writer_trial_t *t = &d->writer_trial;
    epd_ssd1677_trial_summary_t *s = &t->status.trial;
    const bool start = !strcmp(mode, "trial-start"), stop = !strcmp(mode, "trial-stop");
    if ((start && s->active) || (stop && !s->active)) return ESP_ERR_INVALID_STATE;
    if (start) {
        *s = (epd_ssd1677_trial_summary_t){.started=true, .active=true};
        t->trial_last_activation_valid = t->trial_last_ready_valid = false;
        writer_trial_queue(d, true);
        s->start_us = s->end_us = esp_timer_get_time();
    } else if (s->active) {
        writer_trial_queue(d, false);
        s->end_us = esp_timer_get_time();
        if (stop) s->active = false;
    }
    return ESP_OK;
}
