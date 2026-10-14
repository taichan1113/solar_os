#include "ft6336_frame.h"

#include <string.h>

ft6336_frame_result_t ft6336_frame_decode(const uint8_t *data, size_t length,
                                          ft6336_frame_t *out)
{
    if (out == NULL) return FT6336_FRAME_INVALID;
    ft6336_frame_t frame = {0};
    if (data == NULL) frame.reasons = FT6336_FRAME_REASON_ARGUMENT;
    else if (length < FT6336_FRAME_BYTES) frame.reasons = FT6336_FRAME_REASON_LENGTH;
    else {
        memcpy(frame.raw, data, sizeof(frame.raw));
        frame.reported_count = data[0] & 0x0fU;
        if (frame.reported_count > FT6336_FRAME_CONTACTS)
            frame.reasons |= FT6336_FRAME_REASON_COUNT;
        for (size_t i = 0; i < FT6336_FRAME_CONTACTS; ++i) {
            const uint8_t *p = data + 1U + 6U * i;
            ft6336_contact_t *c = &frame.contacts[i];
            c->event = (ft6336_event_t)(p[0] >> 6);
            c->x = (uint16_t)(((uint16_t)(p[0] & 0x0fU) << 8) | p[1]);
            c->y = (uint16_t)(((uint16_t)(p[2] & 0x0fU) << 8) | p[3]);
            c->id = p[2] >> 4;
            c->reported = i < frame.reported_count;
            c->valid = c->reported && c->id != 0x0fU && c->event != FT6336_EVENT_RESERVED;
            if (!c->reported) continue;
            if (c->id == 0x0fU) frame.reasons |= FT6336_FRAME_REASON_ID_SENTINEL;
            if (c->event == FT6336_EVENT_RESERVED)
                frame.reasons |= FT6336_FRAME_REASON_RESERVED_EVENT;
            if (c->event == FT6336_EVENT_UP)
                frame.reasons |= FT6336_FRAME_REASON_UP_COUNT_UNKNOWN;
        }
        if (frame.reported_count >= 2 && frame.contacts[0].id == frame.contacts[1].id) {
            frame.reasons |= FT6336_FRAME_REASON_DUPLICATE_ID;
            frame.contacts[0].valid = frame.contacts[1].valid = false;
        }
        const uint32_t invalid = frame.reasons & ~((uint32_t)FT6336_FRAME_REASON_UP_COUNT_UNKNOWN);
        frame.result = invalid ? FT6336_FRAME_INVALID :
            frame.reasons ? FT6336_FRAME_UNCERTAIN : FT6336_FRAME_OK;
    }
    *out = frame;
    return frame.result;
}
