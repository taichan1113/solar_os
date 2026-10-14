/* Included by solar_os_ft6336.c: no second polling task or I2C owner. */
static portMUX_TYPE raw_lock = portMUX_INITIALIZER_UNLOCKED;
static solar_os_touchraw_status_t raw_status;
static solar_os_touchraw_record_t *raw_records;
static solar_os_touchraw_end_t raw_stop_request;
static solar_os_port_handle_t raw_owner = SOLAR_OS_PORT_HANDLE_INIT;

static bool raw_usb_connected(const solar_os_port_handle_t *owner)
{
    solar_os_cdc_status_t status;
    solar_os_cdc_get_status(&status);
    return owner && solar_os_port_handle_valid(owner) && status.connected && status.port_claimed &&
        !strncmp(status.port_owner, "session:", 8);
}

void solar_os_ft6336_raw_status(solar_os_touchraw_status_t *status)
{
    if (!status) return;
    portENTER_CRITICAL(&raw_lock);
    *status = raw_status;
    portEXIT_CRITICAL(&raw_lock);
}

esp_err_t solar_os_ft6336_raw_start(const solar_os_port_handle_t *owner)
{
    if (!raw_usb_connected(owner)) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&raw_lock);
    const bool allowed = touch.ready && touch.worker_task && !touch.stop_requested &&
        raw_status.mode == TOUCHRAW_IDLE && !raw_status.dumping && !raw_status.reads;
    if (allowed) raw_status.mode = TOUCHRAW_PREPARING;
    portEXIT_CRITICAL(&raw_lock);
    if (!allowed) return ESP_ERR_INVALID_STATE;
    /* Allocation is once per journal lifetime, outside the worker and lock.
     * Refuse missing PSRAM rather than consume critical internal SRAM. */
    if (!raw_records) raw_records = solar_os_memory_alloc(
        sizeof(*raw_records) * SOLAR_OS_TOUCHRAW_CAPACITY,
        SOLAR_OS_MEMORY_EXTERNAL_REQUIRED, "touchraw");
    portENTER_CRITICAL(&raw_lock);
    const bool ready = raw_records && touch.ready && !touch.stop_requested &&
        raw_status.mode == TOUCHRAW_PREPARING && !raw_stop_request;
    if (ready) {
        raw_owner = *owner;
        raw_status = (solar_os_touchraw_status_t){.mode = TOUCHRAW_PENDING};
    } else if (raw_status.mode == TOUCHRAW_PREPARING) {
        raw_status.mode = TOUCHRAW_IDLE;
        raw_status.end = TOUCHRAW_END_START_FAILED;
        raw_stop_request = TOUCHRAW_END_NONE;
    }
    portEXIT_CRITICAL(&raw_lock);
    return ready ? ESP_OK : raw_records ? ESP_ERR_INVALID_STATE : ESP_ERR_NO_MEM;
}

esp_err_t solar_os_ft6336_raw_stop(void)
{
    portENTER_CRITICAL(&raw_lock);
    if (raw_status.mode == TOUCHRAW_RUNNING || raw_status.mode == TOUCHRAW_PENDING ||
        raw_status.mode == TOUCHRAW_PREPARING) raw_stop_request = TOUCHRAW_END_STOP;
    portEXIT_CRITICAL(&raw_lock);
    return ESP_OK;
}

esp_err_t solar_os_ft6336_raw_clear(void)
{
    portENTER_CRITICAL(&raw_lock);
    const bool allowed = (raw_status.mode == TOUCHRAW_IDLE ||
        raw_status.mode == TOUCHRAW_WAIT_RELEASE) && !raw_status.dumping;
    if (allowed) {
        const solar_os_touchraw_mode_t mode = raw_status.mode;
        raw_status = (solar_os_touchraw_status_t){.mode = mode};
        raw_stop_request = TOUCHRAW_END_NONE;
    }
    portEXIT_CRITICAL(&raw_lock);
    return allowed ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t solar_os_ft6336_raw_dump_begin(solar_os_touchraw_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&raw_lock);
    const bool allowed = (raw_status.mode == TOUCHRAW_IDLE ||
        raw_status.mode == TOUCHRAW_WAIT_RELEASE) && !raw_status.dumping;
    if (allowed) {
        raw_status.dumping = true;
        *status = raw_status;
    }
    portEXIT_CRITICAL(&raw_lock);
    return allowed ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t solar_os_ft6336_raw_record(size_t index, solar_os_touchraw_record_t *record)
{
    if (!record) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&raw_lock);
    const bool allowed = raw_status.dumping && index < raw_status.stored;
    if (allowed) *record = raw_records[index];
    portEXIT_CRITICAL(&raw_lock);
    return allowed ? ESP_OK : ESP_ERR_INVALID_STATE;
}
void solar_os_ft6336_raw_dump_end(void)
{
    portENTER_CRITICAL(&raw_lock);
    raw_status.dumping = false;
    portEXIT_CRITICAL(&raw_lock);
}

/* Called only by the worker, or by stop_device AFTER its successful join.
 * WAIT_RELEASE persists across power-off/detach so reattach cannot synthesize
 * PRESS from a finger left down at diagnostic shutdown. */
static void raw_finish(solar_os_touchraw_end_t reason)
{
    portENTER_CRITICAL(&raw_lock);
    if (raw_status.mode == TOUCHRAW_RUNNING) {
        raw_status.mode = TOUCHRAW_WAIT_RELEASE;
        raw_status.end = reason;
    } else if (raw_status.mode == TOUCHRAW_PENDING || raw_status.mode == TOUCHRAW_PREPARING) {
        raw_status.mode = TOUCHRAW_IDLE;
        raw_status.end = reason;
    }
    raw_stop_request = TOUCHRAW_END_NONE;
    portEXIT_CRITICAL(&raw_lock);
}

static void raw_append(const solar_os_touchraw_record_t *record, const ft6336_frame_t *frame)
{
    const int64_t begin = esp_timer_get_time();
    portENTER_CRITICAL(&raw_lock);
    raw_status.reads++;
    if (raw_status.stored < SOLAR_OS_TOUCHRAW_CAPACITY) {
        raw_records[raw_status.stored++] = *record;
    } else {
        raw_status.dropped++;
        raw_stop_request = TOUCHRAW_END_FULL;
    }
    if (record->error != ESP_OK) {
        raw_status.failures++;
        if (++raw_status.consecutive > raw_status.max_consecutive)
            raw_status.max_consecutive = raw_status.consecutive;
        if (raw_status.consecutive >= 3U) raw_stop_request = TOUCHRAW_END_IO;
    } else {
        raw_status.consecutive = 0;
        raw_status.results[frame->result]++;
        raw_status.counts[frame->reported_count <= 2U ? frame->reported_count : 3U]++;
        if (frame->reasons & FT6336_FRAME_REASON_DUPLICATE_ID) raw_status.duplicates++;
        for (size_t i = 0; i < FT6336_FRAME_CONTACTS; i++) {
            const ft6336_contact_t *c = &frame->contacts[i];
            raw_status.flags[c->event]++;
            if (c->reported) raw_status.reported_flags[c->event]++;
            if (c->id == 15U) {
                raw_status.sentinel_slots++;
                if (c->reported) raw_status.reported_sentinel++;
            }
        }
    }
    portEXIT_CRITICAL(&raw_lock);
    const uint32_t elapsed = (uint32_t)(esp_timer_get_time() - begin);
    portENTER_CRITICAL(&raw_lock);
    raw_status.logger_total_us += elapsed;
    if (elapsed > raw_status.logger_max_us) raw_status.logger_max_us = elapsed;
    portEXIT_CRITICAL(&raw_lock);
}

/* true consumes this poll. No legacy read is performed in a diagnostic poll. */
static bool raw_poll(solar_os_ft6336_device_t *device)
{
    solar_os_touchraw_status_t status;
    solar_os_ft6336_raw_status(&status);
    portENTER_CRITICAL(&raw_lock);
    const solar_os_touchraw_end_t requested = raw_stop_request;
    portEXIT_CRITICAL(&raw_lock);
    if (status.mode == TOUCHRAW_PENDING) {
        if (requested || !raw_usb_connected(&raw_owner)) {
            raw_finish(requested ? requested : TOUCHRAW_END_DISCONNECT);
            return true;
        }
        esp_err_t err = solar_os_input_source_set_ready(device->input_source, false);
        if (err == ESP_OK) err = solar_os_input_pointer_source_quiesce(device->input_source);
        if (err != ESP_OK) {
            (void)solar_os_input_source_set_ready(device->input_source, true);
            raw_finish(TOUCHRAW_END_START_FAILED);
            return true;
        }
        device->pressed = false;
        portENTER_CRITICAL(&raw_lock);
        raw_status.mode = TOUCHRAW_RUNNING;
        raw_status.started_us = esp_timer_get_time();
        status = raw_status;
        portEXIT_CRITICAL(&raw_lock);
    }
    if (status.mode == TOUCHRAW_RUNNING) {
        solar_os_touchraw_end_t end = requested;
        if (!end && !raw_usb_connected(&raw_owner)) end = TOUCHRAW_END_DISCONNECT;
        if (!end && esp_timer_get_time() - status.started_us >= SOLAR_OS_TOUCHRAW_TIMEOUT_US)
            end = TOUCHRAW_END_TIMEOUT;
        if (end) { raw_finish(end); return true; }
        ft6336_frame_t frame = {0}; /* Never reuse a failed read's predecessor. */
        solar_os_touchraw_record_t record = {.sequence = status.reads + 1U};
        record.start_us = esp_timer_get_time();
        record.error = ft6336_read_frame(&frame);
        record.end_us = esp_timer_get_time();
        if (record.error == ESP_OK) {
            memcpy(record.raw, frame.raw, sizeof(record.raw));
            record.result = (uint8_t)frame.result;
            record.reasons = frame.reasons;
        }
        raw_append(&record, &frame);
        return true;
    }
    if (status.mode == TOUCHRAW_WAIT_RELEASE) {
        ft6336_sample_t sample = {0};
        if (ft6336_read(&sample) == ESP_OK && !sample.touched &&
            solar_os_input_source_set_ready(device->input_source, true) == ESP_OK) {
            portENTER_CRITICAL(&raw_lock);
            raw_status.mode = TOUCHRAW_IDLE;
            portEXIT_CRITICAL(&raw_lock);
        }
        return true;
    }
    return status.mode == TOUCHRAW_PREPARING;
}
