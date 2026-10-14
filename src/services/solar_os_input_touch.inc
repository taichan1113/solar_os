/* Part of Input Service, sharing input_lock and source ownership. */
typedef struct {
    uint8_t recipient;
    uint64_t generation;
    char target[SOLAR_OS_INPUT_POINTER_TARGET_MAX];
} input_touch_consumer_t;
typedef struct {
    uint64_t epoch, generation, sequence, delivered_sequence, timestamp_us;
    bool armed, cancel_pending, cancel_inflight;
    solar_os_input_touch_frame_t cancel;
    solar_os_input_touch_core_t core;
} input_touch_source_t;

static EXT_RAM_BSS_ATTR input_touch_source_t input_touch_sources[INPUT_SOURCE_MAX];
static EXT_RAM_BSS_ATTR input_touch_consumer_t input_touch_consumers[SOLAR_OS_INPUT_TOUCH_CONSUMER_MAX];
static EXT_RAM_BSS_ATTR solar_os_input_touch_frame_t input_touch_queue[SOLAR_OS_INPUT_TOUCH_QUEUE_MAX];
static size_t input_touch_head, input_touch_count;
static uint64_t input_touch_serial;
static solar_os_input_touch_stats_t input_touch_counters;

/* Never wrap/reuse a token. At exhaustion, admission fails closed until reboot. */
static uint64_t input_touch_token_locked(void)
{ return input_touch_serial == UINT64_MAX ? 0 : ++input_touch_serial; }

static input_touch_consumer_t *input_touch_consumer_locked(uint64_t generation)
{
    for (size_t i = 0; i < SOLAR_OS_INPUT_TOUCH_CONSUMER_MAX; ++i)
        if (generation && input_touch_consumers[i].generation == generation)
            return &input_touch_consumers[i];
    return NULL;
}

static void input_touch_invalidate_locked(size_t index, solar_os_input_touch_cancel_reason_t reason)
{
    input_touch_source_t *s = &input_touch_sources[index];
    input_touch_consumer_t *c = input_touch_consumer_locked(s->generation);
    s->epoch = input_touch_token_locked();
    s->sequence = s->delivered_sequence = s->timestamp_us = 0;
    s->armed = false;
    solar_os_input_touch_core_init(&s->core);
    if (c != NULL && !s->cancel_pending) {
        s->cancel = (solar_os_input_touch_frame_t){
            .source = (solar_os_input_source_t)(index + 1), .recipient = c->recipient,
            .generation = c->generation, .epoch = s->epoch,
            .state = SOLAR_OS_INPUT_TOUCH_CANCEL, .cancel_reason = reason,
            .timestamp_us = (uint64_t)esp_timer_get_time(),
        };
        strlcpy(s->cancel.target, c->target, sizeof(s->cancel.target));
        s->cancel_pending = true;
        s->cancel_inflight = false;
        ++input_touch_counters.cancel;
    }
}

esp_err_t solar_os_input_touch_consumer_register(uint8_t recipient, const char *target,
                                                uint64_t *generation)
{
    if (!target || !target[0] || strlen(target) >= SOLAR_OS_INPUT_POINTER_TARGET_MAX || !generation)
        return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&input_lock);
    input_touch_consumer_t *slot = NULL;
    for (size_t i = 0; i < SOLAR_OS_INPUT_TOUCH_CONSUMER_MAX; ++i) {
        input_touch_consumer_t *c = &input_touch_consumers[i];
        if (c->generation && (!strcmp(c->target, target) || c->recipient == recipient)) {
            portEXIT_CRITICAL(&input_lock); return ESP_ERR_INVALID_STATE;
        }
        if (!c->generation) slot = c;
    }
    const uint64_t token = slot ? input_touch_token_locked() : 0;
    if (!token) { portEXIT_CRITICAL(&input_lock); return ESP_ERR_NO_MEM; }
    *slot = (input_touch_consumer_t){.recipient = recipient, .generation = token};
    strlcpy(slot->target, target, sizeof(slot->target));
    *generation = token;
    portEXIT_CRITICAL(&input_lock);
    return ESP_OK;
}

void solar_os_input_touch_consumer_cancel(uint64_t generation)
{
    portENTER_CRITICAL(&input_lock);
    for (size_t i = 0; i < INPUT_SOURCE_MAX; ++i)
        if (generation && input_touch_sources[i].generation == generation)
            input_touch_invalidate_locked(i, SOLAR_OS_INPUT_TOUCH_CANCEL_LIFECYCLE);
    portEXIT_CRITICAL(&input_lock);
}

void solar_os_input_touch_consumer_unregister(uint64_t generation)
{
    portENTER_CRITICAL(&input_lock);
    input_touch_consumer_t *c = input_touch_consumer_locked(generation);
    if (c != NULL) {
        for (size_t i = 0; i < INPUT_SOURCE_MAX; ++i) {
            input_touch_source_t *s = &input_touch_sources[i];
            if (s->generation != generation && (!s->cancel_pending || s->cancel.generation != generation)) continue;
            input_touch_invalidate_locked(i, SOLAR_OS_INPUT_TOUCH_CANCEL_LIFECYCLE);
            s->generation = 0;
            if (s->cancel_pending) ++input_touch_counters.stale_discard;
            s->cancel_pending = s->cancel_inflight = false;
        }
        memset(c, 0, sizeof(*c));
    }
    portEXIT_CRITICAL(&input_lock);
}

esp_err_t solar_os_input_touch_capture(solar_os_input_source_t source, const char *target,
                                       solar_os_input_touch_ticket_t *ticket)
{
    if (!target || !ticket) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&input_lock);
    if (!input_source_valid_locked(source) || !input_sources[source-1].ready ||
        !(input_sources[source-1].capabilities & SOLAR_OS_INPUT_CAP_TOUCH_FRAMES)) {
        portEXIT_CRITICAL(&input_lock); return ESP_ERR_INVALID_STATE;
    }
    input_touch_consumer_t *c = NULL;
    for (size_t i = 0; i < SOLAR_OS_INPUT_TOUCH_CONSUMER_MAX; ++i)
        if (input_touch_consumers[i].generation && !strcmp(input_touch_consumers[i].target, target))
            c = &input_touch_consumers[i];
    if (!c) { portEXIT_CRITICAL(&input_lock); return ESP_ERR_NOT_FOUND; }
    input_touch_source_t *s = &input_touch_sources[source-1];
    if (s->generation != c->generation) {
        if (s->generation) {
            input_touch_invalidate_locked(source-1, SOLAR_OS_INPUT_TOUCH_CANCEL_LIFECYCLE);
            s->generation = 0; /* Do not issue a second cancel on each capture retry. */
        }
        if (s->cancel_pending) { portEXIT_CRITICAL(&input_lock); return ESP_ERR_INVALID_STATE; }
        s->generation = c->generation;
        s->epoch = input_touch_token_locked();
        s->sequence = s->delivered_sequence = s->timestamp_us = 0;
        s->armed = false; /* Initial admission also requires an observed all-up. */
        solar_os_input_touch_core_init(&s->core);
    }
    if (s->sequence == UINT64_MAX) {
        input_touch_invalidate_locked(source-1, SOLAR_OS_INPUT_TOUCH_CANCEL_SEQUENCE);
        portEXIT_CRITICAL(&input_lock); return ESP_ERR_INVALID_STATE;
    }
    if (!s->epoch) {
        portEXIT_CRITICAL(&input_lock); return ESP_ERR_INVALID_STATE;
    }
    *ticket = (solar_os_input_touch_ticket_t){.source = source, .recipient = c->recipient,
        .epoch = s->epoch, .generation = c->generation, .next_sequence = s->sequence+1};
    strlcpy(ticket->target, c->target, sizeof(ticket->target));
    portEXIT_CRITICAL(&input_lock);
    return ESP_OK;
}

esp_err_t solar_os_input_write_touch_frame(const solar_os_input_touch_ticket_t *ticket,
    uint64_t sequence, uint64_t timestamp_us, const ft6336_frame_t *raw)
{
    if (!ticket || !ticket->source || ticket->source > INPUT_SOURCE_MAX ||
        memchr(ticket->target, 0, sizeof(ticket->target)) == NULL) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&input_lock);
    input_touch_source_t *s = &input_touch_sources[ticket->source-1];
    input_touch_consumer_t *c = input_touch_consumer_locked(ticket->generation);
    if (!input_source_valid_locked(ticket->source) || !input_sources[ticket->source-1].ready ||
        !c || c->recipient != ticket->recipient || strcmp(c->target, ticket->target) ||
        !ticket->epoch || s->epoch != ticket->epoch || s->generation != ticket->generation) {
        ++input_touch_counters.stale_discard;
        portEXIT_CRITICAL(&input_lock); return ESP_ERR_INVALID_STATE;
    }
    if (s->sequence == UINT64_MAX || sequence != s->sequence+1 || sequence != ticket->next_sequence ||
        timestamp_us < s->timestamp_us) {
        input_touch_invalidate_locked(ticket->source-1, SOLAR_OS_INPUT_TOUCH_CANCEL_SEQUENCE);
        portEXIT_CRITICAL(&input_lock); return ESP_ERR_INVALID_STATE;
    }
    s->sequence = sequence; s->timestamp_us = timestamp_us;
    solar_os_touch_transition_t transition;
    const solar_os_touch_result_t result = solar_os_input_touch_core_apply(&s->core, raw, &transition);
    if (result == SOLAR_OS_TOUCH_CANCEL_REQUIRED || result == SOLAR_OS_TOUCH_INVALID_ARGUMENT) {
        input_touch_invalidate_locked(ticket->source-1, transition.reason == SOLAR_OS_TOUCH_REASON_READ_FAILURE ?
            SOLAR_OS_INPUT_TOUCH_CANCEL_READ_FAILURE : SOLAR_OS_INPUT_TOUCH_CANCEL_FRAME);
        portEXIT_CRITICAL(&input_lock); return ESP_ERR_INVALID_STATE;
    }
    if (s->cancel_pending || (!s->armed && transition.current.count)) {
        portEXIT_CRITICAL(&input_lock); return ESP_OK;
    }
    if (!s->armed) s->armed = true;
    if (input_touch_count == SOLAR_OS_INPUT_TOUCH_QUEUE_MAX) {
        ++input_touch_counters.overflow;
        input_touch_invalidate_locked(ticket->source-1, SOLAR_OS_INPUT_TOUCH_CANCEL_OVERFLOW);
        portEXIT_CRITICAL(&input_lock); return ESP_ERR_NO_MEM;
    }
    solar_os_input_touch_frame_t frame = {.source = ticket->source, .recipient = ticket->recipient,
        .epoch = ticket->epoch, .generation = ticket->generation, .sequence = sequence,
        .timestamp_us = timestamp_us, .snapshot = transition.current};
    strlcpy(frame.target, ticket->target, sizeof(frame.target));
    for (size_t i = 0; i < frame.snapshot.count; ++i) {
        solar_os_input_pointer_event_t p = {.mode = SOLAR_OS_INPUT_POINTER_ABSOLUTE,
            .x = (int16_t)frame.snapshot.contacts[i].x, .y = (int16_t)frame.snapshot.contacts[i].y};
        input_apply_calibration_locked(&input_diagnostics[ticket->source-1], &p);
        frame.snapshot.contacts[i].x = (uint16_t)p.x;
        frame.snapshot.contacts[i].y = (uint16_t)p.y;
    }
    input_touch_queue[(input_touch_head+input_touch_count) % SOLAR_OS_INPUT_TOUCH_QUEUE_MAX] = frame;
    ++input_touch_count; ++input_touch_counters.enqueued;
    if (input_touch_count > input_touch_counters.high_watermark) input_touch_counters.high_watermark = input_touch_count;
    portEXIT_CRITICAL(&input_lock); return ESP_OK;
}

static bool input_touch_frame_valid_locked(const solar_os_input_touch_frame_t *frame)
{
    if (!frame || !frame->source || frame->source > INPUT_SOURCE_MAX) return false;
    input_touch_source_t *s = &input_touch_sources[frame->source-1];
    input_touch_consumer_t *c = input_touch_consumer_locked(frame->generation);
    if (!c || c->recipient != frame->recipient) return false;
    if (frame->state == SOLAR_OS_INPUT_TOUCH_CANCEL)
        return s->cancel_pending && s->cancel.epoch == frame->epoch && s->cancel.generation == frame->generation;
    return input_source_valid_locked(frame->source) && input_sources[frame->source-1].ready &&
        s->epoch == frame->epoch && s->generation == frame->generation &&
        !s->cancel_pending && frame->sequence > s->delivered_sequence && frame->sequence <= s->sequence;
}

bool solar_os_input_touch_frame_valid(const solar_os_input_touch_frame_t *frame)
{
    portENTER_CRITICAL(&input_lock);
    const bool valid = input_touch_frame_valid_locked(frame);
    if (!valid) ++input_touch_counters.stale_discard;
    portEXIT_CRITICAL(&input_lock); return valid;
}

static bool input_touch_cancel_read_locked(uint64_t generation, solar_os_input_touch_frame_t *frame)
{
    for (size_t i = 0; i < INPUT_SOURCE_MAX; ++i) {
        input_touch_source_t *s = &input_touch_sources[i];
        if (s->cancel_pending && !s->cancel_inflight && (!generation || s->cancel.generation == generation)) {
            *frame = s->cancel; s->cancel_inflight = true; return true;
        }
    }
    return false;
}

bool solar_os_input_read_touch_cancel(uint64_t generation, solar_os_input_touch_frame_t *frame)
{
    if (!frame || !generation) return false;
    portENTER_CRITICAL(&input_lock);
    const bool found = input_touch_cancel_read_locked(generation, frame);
    portEXIT_CRITICAL(&input_lock); return found;
}

bool solar_os_input_read_touch_frame(solar_os_input_touch_frame_t *frame)
{
    if (!frame) return false;
    portENTER_CRITICAL(&input_lock);
    if (input_touch_cancel_read_locked(0, frame)) { portEXIT_CRITICAL(&input_lock); return true; }
    while (input_touch_count) {
        *frame = input_touch_queue[input_touch_head];
        input_touch_head = (input_touch_head+1) % SOLAR_OS_INPUT_TOUCH_QUEUE_MAX;
        --input_touch_count;
        if (input_touch_frame_valid_locked(frame)) { portEXIT_CRITICAL(&input_lock); return true; }
        ++input_touch_counters.stale_discard;
    }
    portEXIT_CRITICAL(&input_lock); return false;
}

void solar_os_input_touch_finish(const solar_os_input_touch_frame_t *frame, bool delivered)
{
    if (!frame || !frame->source || frame->source > INPUT_SOURCE_MAX) return;
    portENTER_CRITICAL(&input_lock);
    input_touch_source_t *s = &input_touch_sources[frame->source-1];
    if (delivered) ++input_touch_counters.delivered;
    if (input_touch_frame_valid_locked(frame)) {
        if (frame->state == SOLAR_OS_INPUT_TOUCH_CANCEL) {
            if (delivered) s->cancel_pending = s->cancel_inflight = false;
            else s->cancel_inflight = false;
        } else if (delivered) s->delivered_sequence = frame->sequence;
        else input_touch_invalidate_locked(frame->source-1, SOLAR_OS_INPUT_TOUCH_CANCEL_LIFECYCLE);
    }
    portEXIT_CRITICAL(&input_lock);
}

void solar_os_input_touch_stats(solar_os_input_touch_stats_t *stats)
{
    if (!stats) return;
    portENTER_CRITICAL(&input_lock); *stats = input_touch_counters; stats->queued = input_touch_count;
    portEXIT_CRITICAL(&input_lock);
}

esp_err_t solar_os_input_touch_apply_orientation(solar_os_input_touch_frame_t *frame,
    uint16_t width, uint16_t height, uint16_t degrees)
{
    if (!frame || frame->snapshot.count > FT6336_FRAME_CONTACTS) return ESP_ERR_INVALID_ARG;
    solar_os_input_touch_frame_t oriented = *frame;
    solar_os_input_pointer_event_t p = {.mode = SOLAR_OS_INPUT_POINTER_ABSOLUTE};
    if (solar_os_input_pointer_apply_orientation(&p, width, height, degrees) != ESP_OK) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < frame->snapshot.count; ++i) {
        if (frame->snapshot.contacts[i].x >= width || frame->snapshot.contacts[i].y >= height) return ESP_ERR_INVALID_ARG;
        p.x = (int16_t)frame->snapshot.contacts[i].x; p.y = (int16_t)frame->snapshot.contacts[i].y;
        solar_os_input_pointer_apply_orientation(&p, width, height, degrees);
        oriented.snapshot.contacts[i].x = (uint16_t)p.x; oriented.snapshot.contacts[i].y = (uint16_t)p.y;
    }
    *frame = oriented; return ESP_OK;
}
