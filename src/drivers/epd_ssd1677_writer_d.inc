/* Display-wide fastest compensation. Origin metadata is diagnostic only.
 * Zero-initialized mode selects D; counters are never an update budget. */
static atomic_bool writer_d_failed;

static void writer_d_count(uint64_t *value)
{
    if (*value != UINT64_MAX) ++*value;
}

static void writer_d_activation(epd_ssd1677_t *d)
{
    epd_ssd1677_writer_trial_t *t = &d->writer_trial;
    if (t->power_control) { writer_d_count(&t->status.power_controls); return; }
    writer_d_count(&t->activation_sequence);
    if (!t->current.physical) t->current.activation_first_seq = t->activation_sequence;
    t->current.activation_last_seq = t->activation_sequence;
    t->current.physical++;
    if (t->used_d) writer_d_count(&t->status.d_physical);
    else if (t->used_e1) writer_d_count(&t->status.e1_physical);
    else if (t->reference) writer_d_count(&t->status.a_physical);
    else writer_d_count(&t->status.otp_physical);
    if (t->origin.is_writer) {
        writer_d_count(&t->status.writer_physical);
        if (!t->used_d && !t->used_e1 && t->reference) writer_d_count(&t->status.writer_a);
        else if (!t->used_d && !t->used_e1) writer_d_count(&t->status.writer_otp);
    } else writer_d_count(&t->status.other_physical);
}

static bool writer_d_select(epd_ssd1677_t *d, bool cold)
{
    epd_ssd1677_writer_trial_t *t = &d->writer_trial;
    t->used_d = t->used_e1 = t->restored = false;
    t->reference = true;
    if ((t->origin.is_sketch && !t->e1_selected && !t->sketch_d_selected) || t->baseline_selected || atomic_load(&writer_d_failed) ||
        d->refresh_mode != EPD_SSD1677_REFRESH_FASTEST) return false;
    /* A known optical/Shadow baseline is required by the existing dispatcher.
     * Cold custom entry still performs its normal reset/full-RAM preparation,
     * then uses D on the FIRST fastest activation, with unchanged voltages. */
    if (d->faulted || !d->powered || !d->shadow_valid || (!cold && !d->controller_ready)) {
        atomic_store(&writer_d_failed, true);
        t->baseline_selected = true;
        d->faulted = true;
        return false; /* reference_refresh stops before any command */
    }
    if (!d->fastest_tiles ||
        d->fastest_spi_hz != 20000000 || !d->dirty_hold_trial ||
        !d->dirty_hold_dual_sync || !d->dirty_hold_full_width || d->dirty_hold_full_height) {
        t->baseline_selected = true; /* Unsupported, known backend: retain A. */
        return false;
    }
    t->used_e1 = t->e1_selected;
    t->used_d = !t->used_e1;
    writer_d_count(t->used_e1 ? &t->status.e1_attempts : &t->status.attempts);
    return true;
}

#include "epd_ssd1677_trial_stats.inc"

static void writer_d_finish(epd_ssd1677_t *d, esp_err_t err)
{
    epd_ssd1677_writer_trial_t *t = &d->writer_trial;
    if (err != ESP_OK) writer_d_count(&t->status.driver_errors);
    if (err != ESP_OK && !t->baseline_selected) {
        t->baseline_selected = true;
        atomic_store(&writer_d_failed, true);
    }
    if (t->used_d || t->used_e1) {
        if (err == ESP_OK && t->restored) {
            writer_d_count(t->used_e1 ? &t->status.e1_success : &t->status.d_success);
        }
        else {
            writer_d_count(t->used_e1 ? &t->status.e1_errors : &t->status.d_errors);
            atomic_store(&writer_d_failed, true);
            t->baseline_selected = true;
            /* No automatic recovery/physical retry when controller state is unknown. */
            d->faulted = true;
            d->shadow_valid = d->papermono_baseline_valid = d->controller_ready = d->reference_awake = false;
        }
    }
    epd_ssd1677_writer_record_t *r = &t->current;
    r->origin = t->origin;
    r->candidate_d = t->used_d;
    r->candidate_e1 = t->used_e1;
    r->reference = t->reference;
    r->restored = t->restored;
    r->error = err;
    r->end_us = esp_timer_get_time();
    if (r->timing_valid) {
        for (unsigned i = 0; i < d->timing.writes; i++) {
            r->ram_bytes += d->timing.ram_data_bytes[i];
            r->spi_us += d->timing.spi_us[i];
        }
        r->ram_start_us = d->timing.write_start_us[0];
        if (d->timing.writes) r->ram_end_us = d->timing.write_end_us[d->timing.writes - 1];
        r->activation_us = d->timing.activate_us[0];
        r->ready_us = d->timing.ready_us[0];
    }
    if (!r->physical && err == ESP_OK) {
        writer_d_count(&t->status.noop);
        if (t->origin.is_writer) writer_d_count(&t->status.writer_noop);
    }
    writer_d_count(&t->status.records);
    r->seq = t->status.records;
    writer_trial_finish(d, r);
    /* Ring position is independent of the saturating total/sequence counter. */
    t->records[t->status.trace_next] = *r;
    t->status.trace_next = (t->status.trace_next + 1) % SSD1677_WRITER_D_RECORDS;
    if (t->status.trace_count < SSD1677_WRITER_D_RECORDS) ++t->status.trace_count;
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
    if (!strcmp(mode, "candidate_d") || !strcmp(mode, "candidate_e1") || !strcmp(mode, "candidate_d_sketch")) {
        if (atomic_load(&writer_d_failed) ||
            d->faulted || !d->operation_begin || !d->execution_context || !d->papermono_otp ||
            d->refresh_mode != EPD_SSD1677_REFRESH_FASTEST || !d->fastest_tiles ||
            d->fastest_spi_hz != 20000000 || !d->dirty_hold_trial || !d->dirty_hold_dual_sync ||
            !d->dirty_hold_full_width || d->dirty_hold_full_height) err = ESP_ERR_INVALID_STATE;
        else {
            t->baseline_selected = false;
            t->e1_selected = !strcmp(mode, "candidate_e1");
            t->sketch_d_selected = !strcmp(mode, "candidate_d_sketch");
        }
    } else if (!strcmp(mode, "baseline") || !strcmp(mode, "disable")) {
        t->baseline_selected = true;
        t->e1_selected = false;
        t->sketch_d_selected = false;
    }
    else if (!strcmp(mode, "trial-start") || !strcmp(mode, "trial-status") || !strcmp(mode, "trial-stop"))
        err = writer_trial_control(d, mode);
    else if (strcmp(mode, "status") && strcmp(mode, "trace")) err = ESP_ERR_INVALID_ARG;
    t->status.enabled = !t->baseline_selected && !atomic_load(&writer_d_failed) && !d->faulted;
    t->status.candidate_e1 = t->e1_selected;
    t->status.candidate_d_sketch = t->sketch_d_selected;
    t->status.failed = atomic_load(&writer_d_failed);
    if (status) *status = t->status;
    if (records) memcpy(records, t->records, sizeof(t->records));
    operation_end(d, EPD_SSD1677_OP_READ, err);
    return err;
}
