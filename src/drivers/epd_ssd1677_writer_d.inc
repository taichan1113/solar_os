/* Writer-only laboratory mode. The six reservations are process/boot global:
 * driver reset, app exit, mode selection and detach never refund them. */
static atomic_uint writer_d_attempts;
static atomic_bool writer_d_failed;

static void writer_d_activation(epd_ssd1677_t *d)
{
    epd_ssd1677_writer_trial_t *t = &d->writer_trial;
    if (t->power_control) { t->status.power_controls++; return; }
    if (t->origin.is_writer) {
        t->current.physical++;
        t->status.writer_physical++;
        if (t->used_d) t->status.d_physical++;
        else if (t->reference) t->status.writer_a++;
        else t->status.writer_otp++;
    } else t->status.other_physical++;
}

static bool writer_d_reserve(epd_ssd1677_t *d, bool cold)
{
    epd_ssd1677_writer_trial_t *t = &d->writer_trial;
    t->used_d = t->restored = t->cap_transition = false;
    t->reference = true;
    if (!t->origin.is_writer || !t->enabled || cold || atomic_load(&writer_d_failed)) return false;
    /* D is restricted to the requested, unchanged normal rows-dual backend. */
    if (d->faulted || !d->powered || !d->controller_ready || !d->shadow_valid ||
        d->refresh_mode != EPD_SSD1677_REFRESH_FASTEST || !d->fastest_tiles ||
        d->fastest_spi_hz != 20000000 || !d->dirty_hold_trial ||
        !d->dirty_hold_dual_sync || !d->dirty_hold_full_width || d->dirty_hold_full_height) {
        t->enabled = false;
        return false;
    }
    unsigned n = atomic_load(&writer_d_attempts);
    while (n < SSD1677_WRITER_D_LIMIT) {
        if (atomic_compare_exchange_weak(&writer_d_attempts, &n, n + 1)) {
            t->used_d = true;
            if (n + 1 == SSD1677_WRITER_D_LIMIT) {
                t->enabled = false;
                t->cap_transition = true;
            }
            return true;
        }
    }
    t->enabled = false;
    return false;
}

static void writer_d_finish(epd_ssd1677_t *d, esp_err_t err)
{
    epd_ssd1677_writer_trial_t *t = &d->writer_trial;
    if (err != ESP_OK && t->enabled) {
        t->enabled = false;
        atomic_store(&writer_d_failed, true);
    }
    if (t->used_d) {
        if (err == ESP_OK && t->restored) {
            t->status.d_success++;
            if (t->cap_transition)
                ESP_LOGI(TAG, "Writer D limit=6 reached; Baseline restored; subsequent updates use A");
        }
        else {
            t->status.d_errors++;
            atomic_store(&writer_d_failed, true);
            t->enabled = false;
            /* No automatic recovery/physical retry when controller state is unknown. */
            d->faulted = true;
            d->shadow_valid = d->papermono_baseline_valid = d->controller_ready = d->reference_awake = false;
        }
    }
    if (!t->origin.is_writer) return;
    epd_ssd1677_writer_record_t *r = &t->current;
    r->origin = t->origin;
    r->candidate_d = t->used_d;
    r->reference = t->reference;
    r->restored = t->restored;
    r->cap_transition = t->cap_transition;
    r->error = err;
    r->end_us = esp_timer_get_time();
    if (r->physical || t->reference) {
        for (unsigned i = 0; i < d->timing.writes; i++) {
            r->ram_bytes += d->timing.ram_data_bytes[i];
            r->spi_us += d->timing.spi_us[i];
        }
        r->ram_start_us = d->timing.write_start_us[0];
        if (d->timing.writes) r->ram_end_us = d->timing.write_end_us[d->timing.writes - 1];
        r->activation_us = d->timing.activate_us[0];
        r->ready_us = d->timing.ready_us[0];
    }
    if (!r->physical && err == ESP_OK) t->status.writer_noop++;
    r->seq = ++t->status.records;
    t->records[(r->seq - 1) % SSD1677_WRITER_D_RECORDS] = *r;
}

/* Public controls use the SAME executor barrier as existing mode changes.
 * It waits through BUSY, post-RAM sync and Baseline restore. Pending snapshots
 * remain queued and choose the new mode only when execution begins. */
esp_err_t epd_ssd1677_writer_lut(epd_ssd1677_t *d, const char *mode,
    epd_ssd1677_writer_status_t *status, epd_ssd1677_writer_record_t *records)
{
    if (!d || !mode) return ESP_ERR_INVALID_ARG;
    operation_begin(d, EPD_SSD1677_OP_READ);
    epd_ssd1677_writer_trial_t *t = &d->writer_trial;
    esp_err_t err = ESP_OK;
    if (!strcmp(mode, "candidate_d")) {
        if (atomic_load(&writer_d_failed) || atomic_load(&writer_d_attempts) >= SSD1677_WRITER_D_LIMIT ||
            d->faulted || !d->operation_begin || !d->execution_context || !d->papermono_otp ||
            d->refresh_mode != EPD_SSD1677_REFRESH_FASTEST || !d->fastest_tiles ||
            d->fastest_spi_hz != 20000000 || !d->dirty_hold_trial || !d->dirty_hold_dual_sync ||
            !d->dirty_hold_full_width || d->dirty_hold_full_height) err = ESP_ERR_INVALID_STATE;
        else t->enabled = true;
    } else if (!strcmp(mode, "baseline") || !strcmp(mode, "disable")) t->enabled = false;
    else if (strcmp(mode, "status") && strcmp(mode, "trace")) err = ESP_ERR_INVALID_ARG;
    t->status.enabled = t->enabled && !atomic_load(&writer_d_failed) && !d->faulted;
    t->status.failed = atomic_load(&writer_d_failed);
    t->status.attempts = atomic_load(&writer_d_attempts);
    t->status.cap_reached = t->status.attempts >= SSD1677_WRITER_D_LIMIT;
    if (status) *status = t->status;
    if (records) memcpy(records, t->records, sizeof(t->records));
    operation_end(d, EPD_SSD1677_OP_READ, err);
    return err;
}
