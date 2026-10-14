#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "solar_os_input_touch_core.h"

static unsigned cases;
#define RUN(fn) do { fn(); ++cases; } while (0)
static solar_os_input_touch_core_t core;
static solar_os_touch_transition_t change;
static ft6336_frame_t frame;
static uint8_t raw[FT6336_FRAME_BYTES];
static void contact(size_t i, uint8_t id, uint16_t x, uint16_t y)
{
    size_t p = 1 + 6*i;
    raw[p] = (uint8_t)(0x80 | (x >> 8)); raw[p+1] = (uint8_t)x;
    raw[p+2] = (uint8_t)((id << 4) | (y >> 8)); raw[p+3] = (uint8_t)y;
}
static solar_os_touch_result_t apply(void)
{
    ft6336_frame_decode(raw, sizeof(raw), &frame);
    return solar_os_input_touch_core_apply(&core, &frame, &change);
}
static void setup(void)
{ solar_os_input_touch_core_init(&core); memset(raw, 0, sizeof(raw)); }
static void no_edges(void)
{ assert(!change.started.count && !change.moved.count && !change.ended.count); }
static void sequence(void)
{
    setup(); assert(apply() == SOLAR_OS_TOUCH_TRANSITION); no_edges();
    raw[0] = 1; contact(0, 3, 100, 200); assert(apply() == SOLAR_OS_TOUCH_TRANSITION);
    assert(change.started.count == 1 && change.started.contacts[0].id == 3);
    raw[0] = 2; contact(1, 7, 300, 400); apply();
    assert(change.started.count == 1 && change.started.contacts[0].id == 7);
    contact(0, 7, 300, 400); contact(1, 3, 100, 200); apply(); no_edges();
    assert(change.current.count == 2 && change.current.contacts[0].id == 3 && change.current.contacts[1].id == 7);
    raw[0] = 1; apply();
    assert(change.ended.count == 1 && change.ended.contacts[0].id == 3 && !change.started.count);
    assert(core.snapshot.contacts[0].id == 7);
    raw[0] = 0; apply(); assert(change.ended.count == 1 && change.ended.contacts[0].id == 7);
}
static void two_to_empty(void)
{
    setup(); raw[0] = 2; contact(0, 3, 100, 200); contact(1, 7, 300, 400); apply();
    raw[0] = 0; apply(); assert(change.ended.count == 2 && !change.started.count && !core.snapshot.count);
}
static void moves(void)
{
    setup(); raw[0] = 2; contact(0, 3, 100, 200); contact(1, 7, 300, 400); apply();
    contact(0, 3, 101, 200); apply(); assert(change.moved.count == 1 && change.moved.contacts[0].id == 3);
    contact(0, 3, 102, 201); contact(1, 7, 301, 401); apply();
    assert(change.moved.count == 2 && !change.started.count && !change.ended.count);
    apply(); no_edges();
}
static void partial_id_change(void)
{
    setup(); raw[0] = 2; contact(0, 3, 100, 200); contact(1, 7, 300, 400); apply();
    contact(1, 8, 300, 400); assert(apply() == SOLAR_OS_TOUCH_TRANSITION);
    assert(change.started.count == 1 && change.started.contacts[0].id == 8);
    assert(change.ended.count == 1 && change.ended.contacts[0].id == 7 && !change.moved.count);
}
static void replaced_single_id(void)
{
    setup(); raw[0] = 1; contact(0, 3, 100, 200); apply();
    contact(0, 7, 100, 200); assert(apply() == SOLAR_OS_TOUCH_CANCEL_REQUIRED);
    assert(change.reason == SOLAR_OS_TOUCH_REASON_ID_DISCONTINUITY && core.needs_resync && !core.snapshot.count);
    no_edges(); assert(apply() == SOLAR_OS_TOUCH_RESYNCED); no_edges();
    assert(core.snapshot.count == 1 && core.snapshot.contacts[0].id == 7);
}
static void replaced_all_ids(void)
{
    setup(); raw[0] = 2; contact(0, 3, 100, 200); contact(1, 7, 300, 400); apply();
    contact(0, 8, 100, 200); contact(1, 9, 300, 400); assert(apply() == SOLAR_OS_TOUCH_CANCEL_REQUIRED);
    no_edges(); assert(apply() == SOLAR_OS_TOUCH_RESYNCED); no_edges();
    contact(1, 9, 301, 400); assert(apply() == SOLAR_OS_TOUCH_TRANSITION);
    assert(change.moved.count == 1 && change.moved.contacts[0].id == 9);
}
static void errors_and_recovery(void)
{
    for (unsigned kind = 0; kind < 5; ++kind) {
        setup(); raw[0] = 2; contact(0, 3, 100, 200); contact(1, 7, 300, 400); apply();
        switch (kind) {
            case 0: contact(1, 3, 300, 400); break;
            case 1: raw[1] |= 0xc0; break;
            case 2: raw[0] = 3; break;
            case 3: raw[1] = 0x40; break; /* UP/count uncertainty. */
            case 4: contact(0, 15, 100, 200); break;
        }
        assert(apply() == SOLAR_OS_TOUCH_CANCEL_REQUIRED && change.reason == SOLAR_OS_TOUCH_REASON_FRAME);
        assert(change.frame_reasons && !core.snapshot.count && !change.current.count); no_edges();
        assert(apply() == SOLAR_OS_TOUCH_CANCEL_REQUIRED); no_edges();
        raw[0] = 1; contact(0, 7, 300, 400);
        assert(apply() == SOLAR_OS_TOUCH_RESYNCED && change.current.count == 1); no_edges();
        raw[0] = 0; assert(apply() == SOLAR_OS_TOUCH_TRANSITION && change.ended.count == 1);
    }
}
static void read_failure(void)
{
    setup(); raw[0] = 1; contact(0, 3, 100, 200); apply();
    assert(solar_os_input_touch_core_apply(&core, NULL, &change) == SOLAR_OS_TOUCH_CANCEL_REQUIRED);
    assert(change.reason == SOLAR_OS_TOUCH_REASON_READ_FAILURE && !core.snapshot.count); no_edges();
    raw[0] = 0; assert(apply() == SOLAR_OS_TOUCH_RESYNCED); no_edges();
    raw[0] = 1; apply(); assert(change.started.count == 1);
}
static void bad_public_frame(void)
{
    setup(); raw[0] = 1; contact(0, 3, 100, 200); apply();
    frame.reported_count = 255;
    assert(solar_os_input_touch_core_apply(&core, &frame, &change) == SOLAR_OS_TOUCH_CANCEL_REQUIRED); no_edges();
    apply(); frame.contacts[0].id = 14;
    assert(solar_os_input_touch_core_apply(&core, &frame, &change) == SOLAR_OS_TOUCH_CANCEL_REQUIRED); no_edges();
    memset(&frame, 0, sizeof(frame));
    assert(solar_os_input_touch_core_apply(&core, &frame, &change) == SOLAR_OS_TOUCH_CANCEL_REQUIRED); no_edges();
}
static void null_arguments(void)
{
    setup(); solar_os_input_touch_core_init(NULL);
    assert(solar_os_input_touch_core_apply(NULL, &frame, &change) == SOLAR_OS_TOUCH_INVALID_ARGUMENT);
    assert(solar_os_input_touch_core_apply(&core, &frame, NULL) == SOLAR_OS_TOUCH_INVALID_ARGUMENT);
    assert(!core.needs_resync && !core.snapshot.count);
}
static void repeated_down(void)
{
    setup(); raw[0] = 1; contact(0, 3, 100, 200); raw[1] &= 0x3f;
    apply(); assert(change.started.count == 1); apply(); no_edges();
    raw[1] |= 0x80; apply(); no_edges();
}
int main(void)
{
    RUN(sequence); RUN(two_to_empty); RUN(moves); RUN(partial_id_change); RUN(replaced_single_id);
    RUN(replaced_all_ids); RUN(errors_and_recovery); RUN(read_failure); RUN(bad_public_frame);
    RUN(null_arguments); RUN(repeated_down);
    printf("input_touch_core_test: %u cases PASS\n", cases);
}
