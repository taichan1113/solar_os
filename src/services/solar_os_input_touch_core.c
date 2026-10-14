#include "solar_os_input_touch_core.h"

#include <string.h>

void solar_os_input_touch_core_init(solar_os_input_touch_core_t *core)
{
    if (core != NULL) memset(core, 0, sizeof(*core));
}

static const solar_os_touch_contact_t *find_id(const solar_os_touch_snapshot_t *s, uint8_t id)
{
    for (size_t i = 0; i < s->count; ++i)
        if (s->contacts[i].id == id) return &s->contacts[i];
    return NULL;
}

static solar_os_touch_result_t cancel(solar_os_input_touch_core_t *core,
                                      solar_os_touch_transition_t *out,
                                      solar_os_touch_reason_t reason)
{
    memset(&core->snapshot, 0, sizeof(core->snapshot));
    core->needs_resync = true;
    memset(&out->current, 0, sizeof(out->current));
    out->reason = reason;
    return out->result = SOLAR_OS_TOUCH_CANCEL_REQUIRED;
}

solar_os_touch_result_t solar_os_input_touch_core_apply(
    solar_os_input_touch_core_t *core, const ft6336_frame_t *frame,
    solar_os_touch_transition_t *out)
{
    if (core == NULL || out == NULL) return SOLAR_OS_TOUCH_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (frame == NULL) return cancel(core, out, SOLAR_OS_TOUCH_REASON_READ_FAILURE);

    /* Re-decode the preserved wire bytes: malformed/forged public structs cannot
     * turn an invalid acquisition into empty input or overrun fixed arrays. */
    ft6336_frame_t checked;
    ft6336_frame_decode(frame->raw, sizeof(frame->raw), &checked);
    if (frame->result != FT6336_FRAME_OK || frame->reasons != 0 ||
        checked.result != FT6336_FRAME_OK ||
        frame->reported_count != checked.reported_count) {
        out->frame_reasons = frame->reasons | checked.reasons;
        return cancel(core, out, SOLAR_OS_TOUCH_REASON_FRAME);
    }
    for (size_t i = 0; i < checked.reported_count; ++i) {
        const ft6336_contact_t *c = &checked.contacts[i];
        const ft6336_contact_t *original = &frame->contacts[i];
        if (!original->valid || !original->reported || original->id != c->id ||
            original->x != c->x || original->y != c->y || original->event != c->event)
            return cancel(core, out, SOLAR_OS_TOUCH_REASON_FRAME);
        out->current.contacts[out->current.count++] = (solar_os_touch_contact_t){c->id, c->x, c->y};
    }
    if (out->current.count == 2 && out->current.contacts[0].id > out->current.contacts[1].id) {
        const solar_os_touch_contact_t first = out->current.contacts[0];
        out->current.contacts[0] = out->current.contacts[1];
        out->current.contacts[1] = first;
    }
    if (core->needs_resync) {
        core->snapshot = out->current;
        core->needs_resync = false;
        return out->result = SOLAR_OS_TOUCH_RESYNCED;
    }
    size_t shared = 0;
    for (size_t i = 0; i < out->current.count; ++i)
        if (find_id(&core->snapshot, out->current.contacts[i].id) != NULL) ++shared;
    if (core->snapshot.count && out->current.count && !shared)
        return cancel(core, out, SOLAR_OS_TOUCH_REASON_ID_DISCONTINUITY);

    for (size_t i = 0; i < core->snapshot.count; ++i) {
        const solar_os_touch_contact_t *old = &core->snapshot.contacts[i];
        if (find_id(&out->current, old->id) == NULL)
            out->ended.contacts[out->ended.count++] = *old;
    }
    for (size_t i = 0; i < out->current.count; ++i) {
        const solar_os_touch_contact_t *now = &out->current.contacts[i];
        const solar_os_touch_contact_t *old = find_id(&core->snapshot, now->id);
        if (old == NULL) out->started.contacts[out->started.count++] = *now;
        else if (old->x != now->x || old->y != now->y)
            out->moved.contacts[out->moved.count++] = *now;
    }
    core->snapshot = out->current;
    return out->result = SOLAR_OS_TOUCH_TRANSITION;
}
