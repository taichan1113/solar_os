#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ft6336_frame.h"

static unsigned cases;
#define RUN(fn) do { fn(); ++cases; } while (0)
static uint8_t raw[FT6336_FRAME_BYTES];
static ft6336_frame_t frame;

static void point(size_t slot, uint8_t id, uint16_t x, uint16_t y, ft6336_event_t event)
{
    const size_t p = 1 + 6 * slot;
    raw[p] = (uint8_t)((event << 6) | (x >> 8));
    raw[p+1] = (uint8_t)x;
    raw[p+2] = (uint8_t)((id << 4) | (y >> 8));
    raw[p+3] = (uint8_t)y;
    raw[p+4] = 0x52; raw[p+5] = 0xa6; /* Uninterpreted weight/misc preserved. */
}
static ft6336_frame_result_t decode(void)
{ return ft6336_frame_decode(raw, sizeof(raw), &frame); }
static void empty(void)
{
    memset(raw, 0xff, sizeof(raw)); raw[0] = 0;
    assert(decode() == FT6336_FRAME_OK && frame.reported_count == 0 && !frame.reasons);
    assert(!frame.contacts[0].reported && !frame.contacts[1].valid);
    assert(memcmp(raw, frame.raw, sizeof(raw)) == 0);
}
static void one(void)
{
    raw[0] = 1; point(0, 3, 479, 799, FT6336_EVENT_DOWN);
    assert(decode() == FT6336_FRAME_OK && frame.contacts[0].valid);
    assert(frame.contacts[0].id == 3 && frame.contacts[0].x == 479 && frame.contacts[0].y == 799);
    assert(frame.contacts[0].event == FT6336_EVENT_DOWN && !frame.contacts[1].reported);
}
static void two(void)
{
    raw[0] = 2; point(0, 3, 0, 4095, FT6336_EVENT_DOWN); point(1, 7, 4095, 0, FT6336_EVENT_CONTACT);
    assert(decode() == FT6336_FRAME_OK && frame.contacts[1].valid);
    assert(frame.contacts[0].x == 0 && frame.contacts[0].y == 4095);
    assert(frame.contacts[1].id == 7 && frame.contacts[1].x == 4095 && frame.contacts[1].y == 0);
    assert(frame.contacts[1].event == FT6336_EVENT_CONTACT);
}
static void reordered(void)
{
    uint8_t saved[6]; memcpy(saved, raw+1, 6); memcpy(raw+1, raw+7, 6); memcpy(raw+7, saved, 6);
    assert(decode() == FT6336_FRAME_OK && frame.contacts[0].id == 7 && frame.contacts[1].id == 3);
}
static void duplicate(void)
{
    point(1, 7, 100, 200, FT6336_EVENT_CONTACT);
    assert(decode() == FT6336_FRAME_INVALID && (frame.reasons & FT6336_FRAME_REASON_DUPLICATE_ID));
    assert(!frame.contacts[0].valid && !frame.contacts[1].valid);
}
static void sentinel(void)
{
    raw[0] = 1; point(0, 15, 400, 700, FT6336_EVENT_CONTACT);
    assert(decode() == FT6336_FRAME_INVALID && frame.contacts[0].id == 15);
    assert(frame.reasons == FT6336_FRAME_REASON_ID_SENTINEL && !frame.contacts[0].valid);
}
static void count_invalid(void)
{
    point(0, 3, 10, 20, FT6336_EVENT_CONTACT); point(1, 7, 30, 40, FT6336_EVENT_CONTACT);
    for (unsigned n = 3; n <= 15; ++n) {
        raw[0] = (uint8_t)n;
        assert(decode() == FT6336_FRAME_INVALID && frame.reported_count == n);
        assert(frame.reasons == FT6336_FRAME_REASON_COUNT);
    }
}
static void reserved(void)
{
    raw[0] = 1; point(0, 14, 4095, 4095, FT6336_EVENT_RESERVED);
    assert(decode() == FT6336_FRAME_INVALID && !frame.contacts[0].valid);
    assert(frame.reasons == FT6336_FRAME_REASON_RESERVED_EVENT);
}
static void up(void)
{
    raw[0] = 1; point(0, 3, 10, 20, FT6336_EVENT_UP);
    assert(decode() == FT6336_FRAME_UNCERTAIN && frame.reported_count == 1);
    assert(frame.contacts[0].event == FT6336_EVENT_UP && frame.contacts[0].valid);
    assert(frame.reasons == FT6336_FRAME_REASON_UP_COUNT_UNKNOWN);
}
static void mixed_up(void)
{
    raw[0] = 2; point(1, 7, 30, 40, FT6336_EVENT_CONTACT);
    assert(decode() == FT6336_FRAME_UNCERTAIN && frame.contacts[1].valid);
    point(1, 7, 30, 40, FT6336_EVENT_UP);
    assert(decode() == FT6336_FRAME_UNCERTAIN && frame.contacts[1].event == FT6336_EVENT_UP);
}
static void uncounted_up(void)
{
    raw[0] = 0;
    assert(decode() == FT6336_FRAME_OK && frame.reported_count == 0);
    assert(frame.contacts[0].event == FT6336_EVENT_UP && !frame.contacts[0].reported);
    assert(memcmp(frame.raw, raw, sizeof(raw)) == 0);
}
static void multiple_errors(void)
{
    raw[0] = 3; point(0, 15, 10, 20, FT6336_EVENT_RESERVED); point(1, 15, 30, 40, FT6336_EVENT_UP);
    assert(decode() == FT6336_FRAME_INVALID);
    assert(frame.reasons == (FT6336_FRAME_REASON_COUNT | FT6336_FRAME_REASON_DUPLICATE_ID |
        FT6336_FRAME_REASON_ID_SENTINEL | FT6336_FRAME_REASON_RESERVED_EVENT | FT6336_FRAME_REASON_UP_COUNT_UNKNOWN));
}
static void short_buffers(void)
{
    for (size_t n = 0; n < FT6336_FRAME_BYTES; ++n) {
        uint8_t *short_data = malloc(n ? n : 1); assert(short_data);
        memset(&frame, 0xa5, sizeof(frame));
        assert(ft6336_frame_decode(short_data, n, &frame) == FT6336_FRAME_INVALID);
        assert(frame.reasons == FT6336_FRAME_REASON_LENGTH && frame.reported_count == 0);
        for (size_t i = 0; i < FT6336_FRAME_BYTES; ++i) assert(frame.raw[i] == 0);
        free(short_data);
    }
}
static void null_arguments(void)
{
    assert(ft6336_frame_decode(NULL, 13, &frame) == FT6336_FRAME_INVALID);
    assert(frame.reasons == FT6336_FRAME_REASON_ARGUMENT);
    assert(ft6336_frame_decode(raw, 13, NULL) == FT6336_FRAME_INVALID);
    assert(ft6336_frame_decode(NULL, 0, NULL) == FT6336_FRAME_INVALID);
}
static void long_buffer(void)
{
    uint8_t longer[14] = {0}; longer[13] = 0xff;
    assert(ft6336_frame_decode(longer, sizeof(longer), &frame) == FT6336_FRAME_OK);
}
static void all_ids_and_events(void)
{
    /* 1,024 two-slot ID/event combinations, with independent expected result. */
    raw[0] = 2;
    for (unsigned a = 0; a < 16; ++a) for (unsigned b = 0; b < 16; ++b)
        for (unsigned e = 0; e < 4; ++e) {
            point(0, (uint8_t)a, 257, 4095, (ft6336_event_t)e);
            point(1, (uint8_t)b, 4095, 257, FT6336_EVENT_CONTACT);
            const bool invalid = a == b || a == 15 || b == 15 || e == 3;
            const ft6336_frame_result_t expected = invalid ? FT6336_FRAME_INVALID :
                e == 1 ? FT6336_FRAME_UNCERTAIN : FT6336_FRAME_OK;
            assert(decode() == expected && frame.contacts[0].id == a && frame.contacts[1].id == b);
            assert(frame.contacts[0].x == 257 && frame.contacts[1].y == 257);
        }
}
int main(void)
{
    RUN(empty); RUN(one); RUN(two); RUN(reordered); RUN(duplicate); RUN(sentinel);
    RUN(count_invalid); RUN(reserved); RUN(up); RUN(mixed_up); RUN(uncounted_up);
    RUN(multiple_errors); RUN(short_buffers); RUN(null_arguments); RUN(long_buffer); RUN(all_ids_and_events);
    printf("ft6336_frame_test: %u cases PASS (including 1024 ID/event combinations)\n", cases);
}
