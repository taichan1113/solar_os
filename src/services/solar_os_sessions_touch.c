#include "solar_os_sessions_touch.h"

#include <string.h>
#include "esp_attr.h"

_Static_assert(offsetof(solar_os_event_t, data) == 4, "legacy event payload offset");
_Static_assert(sizeof(solar_os_event_t) == 40, "legacy event ABI size");
_Static_assert(sizeof(solar_os_input_pointer_event_t) == 36, "legacy pointer ABI size");

#define TOUCH_SOURCE_MAX 8U
typedef struct {
    bool active, waiting;
    uint8_t id;
    uint16_t x, y;
} touch_primary_t;
typedef struct {
    uint8_t owner;
    bool closing;
    uint64_t generation;
    char target[SOLAR_OS_INPUT_POINTER_TARGET_MAX];
    const solar_os_app_t *app;
    void *identity;
    solar_os_sessions_touch_deliver_t deliver;
    touch_primary_t primary[TOUCH_SOURCE_MAX];
} touch_recipient_t;
static EXT_RAM_BSS_ATTR touch_recipient_t touch_recipients[SOLAR_OS_INPUT_TOUCH_CONSUMER_MAX];

static touch_recipient_t *touch_owner(uint8_t owner)
{
    for (size_t i = 0; i < SOLAR_OS_INPUT_TOUCH_CONSUMER_MAX; ++i)
        if (touch_recipients[i].generation && touch_recipients[i].owner == owner) return &touch_recipients[i];
    return NULL;
}

esp_err_t solar_os_sessions_touch_bind(uint8_t owner, const char *target,
    const solar_os_app_t *app, void *identity, solar_os_sessions_touch_deliver_t deliver)
{
    if (!target || !app || !deliver || strlen(target) >= SOLAR_OS_INPUT_POINTER_TARGET_MAX)
        return ESP_ERR_INVALID_ARG;
    if (!(app->flags & (SOLAR_OS_APP_FLAG_TOUCH_FRAMES | SOLAR_OS_APP_FLAG_POINTER_EVENTS))) {
        solar_os_sessions_touch_unbind(owner);
        return ESP_ERR_NOT_SUPPORTED;
    }
    touch_recipient_t *r = touch_owner(owner);
    if (r && r->closing) return ESP_ERR_INVALID_STATE;
    if (r && r->app == app && r->identity == identity && r->deliver == deliver && !strcmp(r->target, target))
        return ESP_OK;
    if (r) solar_os_sessions_touch_unbind(owner);
    r = NULL;
    for (size_t i = 0; i < SOLAR_OS_INPUT_TOUCH_CONSUMER_MAX; ++i)
        if (!touch_recipients[i].generation) { r = &touch_recipients[i]; break; }
    if (!r) return ESP_ERR_NO_MEM;
    uint64_t generation;
    esp_err_t err = solar_os_input_touch_consumer_register(owner, target, &generation);
    if (err != ESP_OK) return err;
    *r = (touch_recipient_t){.owner = owner, .generation = generation, .app = app,
        .identity = identity, .deliver = deliver};
    memcpy(r->target, target, strlen(target)+1);
    return ESP_OK;
}

static bool touch_cancel_deliver(touch_recipient_t *r, const solar_os_input_touch_frame_t *frame)
{
    memset(&r->primary[frame->source-1], 0, sizeof(r->primary[0]));
    const solar_os_event_t event = {
        .type = (r->app->flags & SOLAR_OS_APP_FLAG_TOUCH_FRAMES) ?
            SOLAR_OS_EVENT_TOUCH_FRAME : SOLAR_OS_EVENT_TOUCH_CANCEL,
        .data.touch_frame = frame,
    };
    return r->deliver(r->identity, &event);
}

void solar_os_sessions_touch_unbind(uint8_t owner)
{
    touch_recipient_t *r = touch_owner(owner);
    if (!r || r->closing) return;
    r->closing = true;
    solar_os_input_touch_consumer_cancel(r->generation);
    solar_os_input_touch_frame_t frame;
    while (solar_os_input_read_touch_cancel(r->generation, &frame)) {
        const bool valid = solar_os_input_touch_frame_valid(&frame);
        const bool delivered = valid && touch_cancel_deliver(r, &frame);
        solar_os_input_touch_finish(&frame, delivered);
        /* A gone recipient must be retired, not retried through its callback. */
        if (!delivered) break;
    }
    solar_os_input_touch_consumer_unregister(r->generation);
    memset(r, 0, sizeof(*r));
}

void solar_os_sessions_touch_unbind_all(void)
{
    for (size_t i = 0; i < SOLAR_OS_INPUT_TOUCH_CONSUMER_MAX; ++i)
        if (touch_recipients[i].generation) solar_os_sessions_touch_unbind(touch_recipients[i].owner);
}

bool solar_os_sessions_touch_dispatch(const solar_os_input_touch_frame_t *frame,
    uint16_t width, uint16_t height, uint16_t orientation)
{
    if (!frame || !solar_os_input_touch_frame_valid(frame)) return false;
    touch_recipient_t *r = touch_owner(frame->recipient);
    if (!r || r->closing || r->generation != frame->generation || strcmp(r->target, frame->target)) return false;
    if (frame->state == SOLAR_OS_INPUT_TOUCH_CANCEL) return touch_cancel_deliver(r, frame);
    solar_os_input_touch_frame_t oriented = *frame;
    if (solar_os_input_touch_apply_orientation(&oriented, width, height, orientation) != ESP_OK) {
        solar_os_input_touch_consumer_cancel(r->generation);
        return false;
    }
    if (r->app->flags & SOLAR_OS_APP_FLAG_TOUCH_FRAMES) {
        const solar_os_event_t event = {.type = SOLAR_OS_EVENT_TOUCH_FRAME, .data.touch_frame = &oriented};
        return r->deliver(r->identity, &event);
    }
    touch_primary_t *p = &r->primary[frame->source-1];
    const solar_os_touch_contact_t *contact = NULL;
    for (size_t i = 0; i < oriented.snapshot.count; ++i)
        if (p->active && oriented.snapshot.contacts[i].id == p->id) contact = &oriented.snapshot.contacts[i];
    solar_os_input_pointer_action_t action = SOLAR_OS_INPUT_POINTER_MOVE;
    if (p->active && !contact) {
        p->active = false; p->waiting = oriented.snapshot.count != 0;
        action = SOLAR_OS_INPUT_POINTER_RELEASE;
    } else if (!p->active) {
        if (!oriented.snapshot.count) { p->waiting = false; return true; }
        if (p->waiting) return true;
        contact = &oriented.snapshot.contacts[0];
        p->active = true; p->id = contact->id; action = SOLAR_OS_INPUT_POINTER_PRESS;
    } else if (contact->x == p->x && contact->y == p->y) return true;
    if (contact) { p->x = contact->x; p->y = contact->y; }
    solar_os_event_t event = {.type = SOLAR_OS_EVENT_POINTER, .data.pointer = {
        .source = frame->source, .pointer_id = p->id, .action = action,
        .mode = SOLAR_OS_INPUT_POINTER_ABSOLUTE,
        .buttons = p->active ? SOLAR_OS_INPUT_POINTER_BUTTON_PRIMARY : 0,
        .x = (int16_t)p->x, .y = (int16_t)p->y,
    }};
    memcpy(event.data.pointer.target, oriented.target, sizeof(oriented.target));
    return r->deliver(r->identity, &event);
}
