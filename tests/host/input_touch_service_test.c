/* Reuse the established NVS/memory/timer fixture, link actual Input and Session
 * touch services. No queue/tracking algorithm is reimplemented in this test. */
#define main legacy_input_main
#include "input_test.c"
#undef main

#include "solar_os_sessions_touch.h"
#include "../../src/services/solar_os_input.c"
_Static_assert(offsetof(solar_os_event_t, data)==4, "legacy event data offset");
_Static_assert(sizeof(solar_os_input_pointer_event_t)==36, "legacy pointer ABI");
_Static_assert(SOLAR_OS_EVENT_RESUME==6, "legacy event enum values");
static unsigned cases, calls, cancels, pointers, frames;
static bool alive, stop_in_callback;
static solar_os_event_t seen;
static solar_os_input_touch_frame_t seen_frame;
static solar_os_input_source_t source;
static solar_os_input_touch_ticket_t ticket;
static uint8_t raw[13];
static ft6336_frame_t decoded;

static const solar_os_app_t frame_app = {.name="frames", .flags=SOLAR_OS_APP_FLAG_TOUCH_FRAMES | SOLAR_OS_APP_FLAG_POINTER_EVENTS};
static const solar_os_app_t pointer_app = {.name="pointer", .flags=SOLAR_OS_APP_FLAG_POINTER_EVENTS};
static bool receive(void *identity, const solar_os_event_t *event)
{
    assert(alive && identity == &alive);
    ++calls; seen = *event;
    if (event->type == SOLAR_OS_EVENT_TOUCH_FRAME || event->type == SOLAR_OS_EVENT_TOUCH_CANCEL) {
        seen_frame = *event->data.touch_frame; seen.data.touch_frame = &seen_frame;
    }
    if (event->type == SOLAR_OS_EVENT_POINTER) ++pointers;
    else if (event->type == SOLAR_OS_EVENT_TOUCH_CANCEL || event->data.touch_frame->state == SOLAR_OS_INPUT_TOUCH_CANCEL) ++cancels;
    else { assert(event->type == SOLAR_OS_EVENT_TOUCH_FRAME); ++frames; }
    if (stop_in_callback && event->type == SOLAR_OS_EVENT_TOUCH_FRAME &&
        event->data.touch_frame->state == SOLAR_OS_INPUT_TOUCH_SAMPLE) {
        stop_in_callback=false; solar_os_sessions_touch_unbind(1); alive=false;
    }
    return true;
}
static void point(size_t i, uint8_t id, uint16_t x, uint16_t y)
{
    size_t p = 1+6*i;
    raw[p]=(uint8_t)(0x80 | (x>>8)); raw[p+1]=(uint8_t)x;
    raw[p+2]=(uint8_t)((id<<4) | (y>>8)); raw[p+3]=(uint8_t)y;
}
static esp_err_t send(uint8_t count)
{
    raw[0]=count; ft6336_frame_decode(raw,sizeof(raw),&decoded);
    assert(solar_os_input_touch_capture(source,"display0",&ticket)==ESP_OK);
    now_us+=10000;
    return solar_os_input_write_touch_frame(&ticket,ticket.next_sequence,(uint64_t)now_us,&decoded);
}
static bool dispatch(uint16_t rotation)
{
    solar_os_input_touch_frame_t f;
    if (!solar_os_input_read_touch_frame(&f)) return false;
    bool delivered=solar_os_sessions_touch_dispatch(&f,480,800,rotation);
    solar_os_input_touch_finish(&f,delivered);
    return true;
}
static void setup(const solar_os_app_t *app)
{
    solar_os_sessions_touch_unbind_all();
    if (source) solar_os_input_source_close(source);
    solar_os_input_touch_frame_t discarded;
    while(solar_os_input_read_touch_frame(&discarded)) solar_os_input_touch_finish(&discarded,false);
    source=0; alive=true; stop_in_callback=false; calls=cancels=pointers=frames=0;
    memset(raw,0,sizeof(raw));
    assert(solar_os_input_source_open_typed("touch-new",SOLAR_OS_INPUT_SOURCE_TOUCH,
        SOLAR_OS_INPUT_CAP_TOUCH_FRAMES | SOLAR_OS_INPUT_CAP_POINTER_ABSOLUTE,true,&source)==ESP_OK);
    assert(solar_os_sessions_touch_bind(1,"display0",app,&alive,receive)==ESP_OK);
}
static void arm(void)
{ assert(send(0)==ESP_OK && dispatch(0)); calls=cancels=pointers=frames=0; }
static void normal_frames(void)
{
    setup(&frame_app); arm();
    point(0,3,20,30); assert(send(1)==ESP_OK && dispatch(0));
    assert(frames==1 && pointers==0 && seen.data.touch_frame->snapshot.count==1);
    point(1,7,40,50); assert(send(2)==ESP_OK && dispatch(0));
    assert(seen.data.touch_frame->snapshot.count==2 && seen.data.touch_frame->snapshot.contacts[1].id==7);
    assert(send(2)==ESP_OK && dispatch(0)); /* Stationary still delivered. */
    point(0,7,40,50); point(1,3,20,30); assert(send(2)==ESP_OK && dispatch(0));
    assert(seen.data.touch_frame->snapshot.contacts[0].id==3);
    assert(send(0)==ESP_OK && dispatch(0));
    assert(frames==5 && !pointers && seen.data.touch_frame->snapshot.count==0);
}
static void initial_gate(void)
{
    setup(&frame_app); point(0,3,20,30);
    assert(send(1)==ESP_OK && !dispatch(0) && !calls);
    arm(); assert(send(1)==ESP_OK && dispatch(0) && frames==1);
}
static void overflow_and_gate(void)
{
    setup(&frame_app); arm(); point(0,3,20,30); point(1,7,40,50);
    solar_os_input_touch_stats_t before, after;
    solar_os_input_touch_stats(&before);
    for(size_t i=0;i<SOLAR_OS_INPUT_TOUCH_QUEUE_MAX;++i) assert(send(2)==ESP_OK);
    solar_os_input_touch_ticket_t old=ticket;
    assert(send(2)==ESP_ERR_NO_MEM);
    solar_os_input_touch_stats(&after);
    assert(after.queued==32 && after.high_watermark==32 && after.overflow==before.overflow+1);
    assert(dispatch(0) && cancels==1 && calls==1); /* priority, no half frame. */
    assert(!dispatch(0)); /* All old epochs discarded. */
    assert(solar_os_input_write_touch_frame(&old,old.next_sequence,(uint64_t)now_us,&decoded)==ESP_ERR_INVALID_STATE);
    assert(send(2)==ESP_OK && !dispatch(0)); /* Held contacts cannot re-arm. */
    assert(send(0)==ESP_OK && dispatch(0));
    assert(send(1)==ESP_OK && dispatch(0) && frames==2);
}
static void cancellation_ack_gate(void)
{
    setup(&frame_app); arm(); point(0,3,20,30); send(1); dispatch(0);
    solar_os_input_touch_consumer_cancel(ticket.generation);
    solar_os_input_touch_frame_t cancel;
    assert(solar_os_input_read_touch_frame(&cancel) && cancel.state==SOLAR_OS_INPUT_TOUCH_CANCEL);
    assert(send(0)==ESP_OK && !dispatch(0)); /* No re-arm before CANCEL completion. */
    solar_os_input_touch_finish(&cancel,false);
    assert(dispatch(0)); /* Cancellation retried, not lost. */
    assert(send(1)==ESP_OK && !dispatch(0));
    assert(send(0)==ESP_OK && dispatch(0));
    assert(send(1)==ESP_OK && dispatch(0));
}
static void invalid_and_failure(void)
{
    for(unsigned kind=0;kind<4;++kind) {
        setup(&frame_app); arm(); point(0,3,20,30); point(1,7,40,50); send(2); dispatch(0);
        if(kind==0) point(1,3,40,50);
        if(kind==1) raw[1]=0xc0;
        if(kind==2) raw[1]=0x40;
        assert(solar_os_input_touch_capture(source,"display0",&ticket)==ESP_OK);
        raw[0]=2; ft6336_frame_decode(raw,sizeof(raw),&decoded);
        assert(solar_os_input_write_touch_frame(&ticket,ticket.next_sequence,(uint64_t)now_us,
            kind==3?NULL:&decoded)==ESP_ERR_INVALID_STATE);
        assert(dispatch(0) && seen.data.touch_frame->state==SOLAR_OS_INPUT_TOUCH_CANCEL);
        assert(seen.data.touch_frame->cancel_reason == (kind==3 ? SOLAR_OS_INPUT_TOUCH_CANCEL_READ_FAILURE : SOLAR_OS_INPUT_TOUCH_CANCEL_FRAME));
        assert(!dispatch(0)); point(0,3,20,30); point(1,7,40,50);
        assert(send(2)==ESP_OK && !dispatch(0)); arm(); assert(send(2)==ESP_OK && dispatch(0));
    }
}
static void sequences(void)
{
    setup(&frame_app); arm(); point(0,3,20,30); send(1);
    solar_os_input_touch_frame_t f; assert(solar_os_input_read_touch_frame(&f));
    assert(solar_os_input_touch_frame_valid(&f));
    assert(solar_os_sessions_touch_dispatch(&f,480,800,0)); solar_os_input_touch_finish(&f,true);
    assert(!solar_os_input_touch_frame_valid(&f)); /* Replay. */
    assert(solar_os_input_write_touch_frame(&ticket,ticket.next_sequence,(uint64_t)now_us,&decoded)==ESP_ERR_INVALID_STATE);
    assert(dispatch(0) && seen.data.touch_frame->cancel_reason==SOLAR_OS_INPUT_TOUCH_CANCEL_SEQUENCE);
}
static void source_reopen(void)
{
    setup(&frame_app); arm(); point(0,3,20,30); send(1);
    solar_os_input_touch_ticket_t old=ticket;
    solar_os_input_source_t id=source; solar_os_input_source_close(source);
    assert(dispatch(0) && cancels==1 && !dispatch(0));
    assert(solar_os_input_source_open_typed("touch-new",SOLAR_OS_INPUT_SOURCE_TOUCH,
        SOLAR_OS_INPUT_CAP_TOUCH_FRAMES | SOLAR_OS_INPUT_CAP_POINTER_ABSOLUTE,true,&source)==ESP_OK && source==id);
    assert(solar_os_input_write_touch_frame(&old,old.next_sequence,(uint64_t)now_us,&decoded)==ESP_ERR_INVALID_STATE);
    assert(send(1)==ESP_OK && !dispatch(0)); arm(); assert(send(1)==ESP_OK && dispatch(0));
}
static void recipient_reuse(void)
{
    setup(&frame_app); arm(); point(0,3,20,30); send(1);
    solar_os_input_touch_ticket_t old=ticket;
    solar_os_sessions_touch_unbind(1); assert(cancels==1); alive=false;
    assert(!dispatch(0)); alive=true;
    assert(solar_os_sessions_touch_bind(1,"display0",&frame_app,&alive,receive)==ESP_OK);
    assert(send(1)==ESP_OK && ticket.generation!=old.generation && !dispatch(0));
    assert(solar_os_input_write_touch_frame(&old,old.next_sequence,(uint64_t)now_us,&decoded)==ESP_ERR_INVALID_STATE);
    arm(); assert(send(1)==ESP_OK && dispatch(0));
}
static void switch_during_callback(void)
{
    setup(&frame_app); arm(); point(0,3,20,30); send(1); send(1);
    stop_in_callback=true; assert(dispatch(0) && !alive && cancels==1);
    assert(!dispatch(0));
}
static void focus_and_suspend(void)
{
    setup(&frame_app); arm(); point(0,3,20,30); send(1); dispatch(0);
    solar_os_sessions_touch_unbind_all(); assert(cancels==1);
    assert(solar_os_sessions_touch_bind(2,"display0",&frame_app,&alive,receive)==ESP_OK);
    assert(send(1)==ESP_OK && !dispatch(0)); arm(); send(1); dispatch(0);
    solar_os_sessions_touch_unbind(2); assert(cancels==1); /* arm resets counter. */
    alive=false; assert(!dispatch(0));
}
static void legacy_projection(void)
{
    setup(&pointer_app); arm(); point(0,3,20,30); send(1); dispatch(0);
    assert(pointers==1 && seen.data.pointer.action==SOLAR_OS_INPUT_POINTER_PRESS && seen.data.pointer.pointer_id==3);
    assert(seen.data.pointer.source==source && !strcmp(seen.data.pointer.target,"display0"));
    point(1,7,40,50); send(2); dispatch(0); assert(pointers==1);
    point(1,7,41,50); send(2); dispatch(0); assert(pointers==1);
    point(0,3,21,31); send(2); dispatch(0); assert(pointers==2 && seen.data.pointer.action==SOLAR_OS_INPUT_POINTER_MOVE);
    point(0,7,41,50); send(1); dispatch(0);
    assert(pointers==3 && seen.data.pointer.action==SOLAR_OS_INPUT_POINTER_RELEASE && seen.data.pointer.buttons==0);
    send(1); dispatch(0); assert(pointers==3); send(0); dispatch(0);
    point(0,7,42,50); send(1); dispatch(0); assert(pointers==4 && seen.data.pointer.pointer_id==7);
    send(0); dispatch(0); assert(pointers==5);
}
static void legacy_secondary_first(void)
{
    setup(&pointer_app); arm(); point(0,3,20,30); send(1); dispatch(0);
    point(1,7,40,50); send(2); dispatch(0); send(1); dispatch(0); assert(pointers==1);
    send(0); dispatch(0); assert(pointers==2 && seen.data.pointer.action==SOLAR_OS_INPUT_POINTER_RELEASE);
}
static void legacy_cancel(void)
{
    setup(&pointer_app); arm(); point(0,3,20,30); send(1); dispatch(0);
    solar_os_input_touch_consumer_cancel(ticket.generation); dispatch(0);
    assert(seen.type==SOLAR_OS_EVENT_TOUCH_CANCEL && cancels==1 && pointers==1);
    assert(send(1)==ESP_OK && !dispatch(0)); arm(); send(1); dispatch(0);
    assert(seen.data.pointer.action==SOLAR_OS_INPUT_POINTER_PRESS);
}
static void rotations(void)
{
    const uint16_t rotation[]={0,90,180,270};
    const uint16_t x[]={20,30,459,769}, y[]={30,459,769,20};
    const uint16_t x2[]={40,50,439,749}, y2[]={50,439,749,40};
    for(unsigned k=0;k<4;++k) {
        setup(&frame_app); arm(); point(0,3,20,30); point(1,7,40,50); send(2); dispatch(rotation[k]);
        assert(seen.data.touch_frame->snapshot.contacts[0].x==x[k] && seen.data.touch_frame->snapshot.contacts[0].y==y[k]);
        assert(seen.data.touch_frame->snapshot.contacts[1].x==x2[k] && seen.data.touch_frame->snapshot.contacts[1].y==y2[k]);
        setup(&pointer_app); arm(); point(0,3,20,30); send(1); dispatch(rotation[k]);
        assert(seen.data.pointer.x==x[k] && seen.data.pointer.y==y[k]);
    }
}
static void removed_recipient(void)
{
    setup(&frame_app); arm(); point(0,3,20,30); send(1);
    solar_os_input_touch_consumer_unregister(ticket.generation); alive=false;
    assert(!dispatch(0));
}
static void acquired_before_switch(void)
{
    setup(&frame_app); arm();
    assert(solar_os_input_touch_capture(source,"display0",&ticket)==ESP_OK);
    solar_os_input_touch_ticket_t acquired=ticket;
    solar_os_sessions_touch_unbind_all();
    assert(solar_os_sessions_touch_bind(1,"display0",&frame_app,&alive,receive)==ESP_OK);
    point(0,3,20,30); raw[0]=1; ft6336_frame_decode(raw,13,&decoded);
    assert(solar_os_input_write_touch_frame(&acquired,acquired.next_sequence,(uint64_t)now_us,&decoded)==ESP_ERR_INVALID_STATE);
    assert(!dispatch(0));
}
static void local_batch_stale(void)
{
    setup(&frame_app); arm(); point(0,3,20,30); send(1);
    solar_os_input_touch_frame_t local; assert(solar_os_input_read_touch_frame(&local));
    solar_os_sessions_touch_unbind(1); alive=false;
    assert(!solar_os_sessions_touch_dispatch(&local,480,800,0));
    solar_os_input_touch_finish(&local,false);
    alive=true; assert(solar_os_sessions_touch_bind(1,"display0",&frame_app,&alive,receive)==ESP_OK);
    assert(send(1)==ESP_OK && !dispatch(0));
}
static void monotonic_contract(void)
{
    setup(&frame_app); arm(); point(0,3,20,30);
    ft6336_frame_decode(raw,13,&decoded);
    assert(solar_os_input_touch_capture(source,"display0",&ticket)==ESP_OK);
    assert(solar_os_input_write_touch_frame(&ticket,ticket.next_sequence+1,(uint64_t)now_us,&decoded)==ESP_ERR_INVALID_STATE);
    assert(dispatch(0)); arm(); send(1); dispatch(0);
    assert(solar_os_input_touch_capture(source,"display0",&ticket)==ESP_OK);
    assert(solar_os_input_write_touch_frame(&ticket,ticket.next_sequence,(uint64_t)now_us-1,&decoded)==ESP_ERR_INVALID_STATE);
    assert(dispatch(0) && seen.data.touch_frame->cancel_reason==SOLAR_OS_INPUT_TOUCH_CANCEL_SEQUENCE);
}
static void calibration_and_double_delivery(void)
{
    setup(&frame_app); arm();
    solar_os_input_pointer_calibration_t c={.min_x=0,.max_x=1000,.min_y=0,.max_y=1000,.width=480,.height=800};
    assert(solar_os_input_pointer_calibration_set(source,&c)==ESP_OK);
    point(0,3,500,500); point(1,7,1000,1000); send(2); dispatch(0);
    assert(seen.data.touch_frame->snapshot.contacts[0].x==240 && seen.data.touch_frame->snapshot.contacts[0].y==400);
    assert(seen.data.touch_frame->snapshot.contacts[1].x==479 && seen.data.touch_frame->snapshot.contacts[1].y==799);
    solar_os_input_pointer_event_t p={.mode=SOLAR_OS_INPUT_POINTER_ABSOLUTE,.action=SOLAR_OS_INPUT_POINTER_PRESS};
    assert(solar_os_input_write_pointer(source,&p)==ESP_ERR_INVALID_STATE);
    assert(!solar_os_input_read_pointer_events(&p,1));
    assert(solar_os_input_pointer_calibration_reset(source)==ESP_OK);
}
static void target_handoff(void)
{
    setup(&frame_app); arm(); point(0,3,20,30); send(1); dispatch(0);
    assert(solar_os_sessions_touch_bind(2,"display1",&frame_app,&alive,receive)==ESP_OK);
    assert(solar_os_input_touch_capture(source,"display1",&ticket)==ESP_ERR_INVALID_STATE);
    assert(dispatch(0) && cancels==1);
    assert(solar_os_input_touch_capture(source,"display1",&ticket)==ESP_OK);
    raw[0]=1; ft6336_frame_decode(raw,13,&decoded);
    assert(solar_os_input_write_touch_frame(&ticket,ticket.next_sequence,(uint64_t)now_us,&decoded)==ESP_OK && !dispatch(0));
}
static void source_ready_and_multi_source(void)
{
    setup(&frame_app); arm(); point(0,3,20,30); send(1);
    solar_os_input_source_t second;
    assert(solar_os_input_source_open_typed("touch-two",SOLAR_OS_INPUT_SOURCE_TOUCH,
        SOLAR_OS_INPUT_CAP_TOUCH_FRAMES,true,&second)==ESP_OK);
    solar_os_input_touch_ticket_t other;
    assert(solar_os_input_touch_capture(second,"display0",&other)==ESP_OK);
    uint8_t zero[13]={0}; ft6336_frame_t empty; ft6336_frame_decode(zero,13,&empty);
    assert(solar_os_input_write_touch_frame(&other,other.next_sequence,(uint64_t)now_us,&empty)==ESP_OK);
    assert(solar_os_input_source_set_ready(source,false)==ESP_OK);
    assert(dispatch(0) && cancels==1);
    assert(dispatch(0) && seen.data.touch_frame->source==second && !dispatch(0));
    assert(solar_os_input_source_set_ready(source,true)==ESP_OK);
    assert(send(1)==ESP_OK && !dispatch(0));
    solar_os_input_source_close(second);
    assert(dispatch(0) && !dispatch(0));
}
static void capacities_and_wrap(void)
{
    setup(&frame_app);
    for(uint8_t id=2;id<=8;++id) {
        char name[16]; snprintf(name,sizeof(name),"display%u",id);
        assert(solar_os_sessions_touch_bind(id,name,&frame_app,&alive,receive)==ESP_OK);
    }
    assert(solar_os_sessions_touch_bind(9,"extra",&frame_app,&alive,receive)==ESP_ERR_NO_MEM);
    solar_os_sessions_touch_unbind_all(); solar_os_input_source_close(source); source=0;
    const uint64_t saved=input_touch_serial; input_touch_serial=UINT64_MAX;
    assert(solar_os_sessions_touch_bind(1,"display0",&frame_app,&alive,receive)==ESP_ERR_NO_MEM);
    input_touch_serial=saved; /* Host fixture only, no product reset API. */
}
static void legacy_source_isolation(void)
{
    setup(&pointer_app); arm(); point(0,3,20,30); send(1); dispatch(0);
    solar_os_input_source_t first=source, second;
    assert(solar_os_input_source_open_typed("touch-other",SOLAR_OS_INPUT_SOURCE_TOUCH,
        SOLAR_OS_INPUT_CAP_TOUCH_FRAMES,true,&second)==ESP_OK);
    source=second; arm(); point(0,7,40,50); send(1); dispatch(0);
    source=first; assert(solar_os_input_source_set_ready(first,false)==ESP_OK);
    assert(dispatch(0) && cancels==1);
    unsigned previous=pointers; source=second;
    assert(send(1)==ESP_OK && dispatch(0) && pointers==previous);
    solar_os_input_source_close(second); dispatch(0); source=first;
}
static void legacy_reordered_ids(void)
{
    setup(&pointer_app); arm(); point(0,3,20,30); send(1); dispatch(0);
    point(0,7,40,50); point(1,3,20,30); send(2); dispatch(0); assert(pointers==1);
    point(1,3,21,31); send(2); dispatch(0);
    assert(pointers==2 && seen.data.pointer.pointer_id==3 && seen.data.pointer.x==21);
}
static void sequence_exhaustion(void)
{
    setup(&frame_app); arm(); point(0,3,20,30); send(1); dispatch(0);
    input_touch_sources[source-1].sequence=UINT64_MAX;
    assert(solar_os_input_touch_capture(source,"display0",&ticket)==ESP_ERR_INVALID_STATE);
    assert(dispatch(0) && seen.data.touch_frame->cancel_reason==SOLAR_OS_INPUT_TOUCH_CANCEL_SEQUENCE);
    assert(send(1)==ESP_OK && !dispatch(0)); arm(); assert(send(1)==ESP_OK && dispatch(0));
}
static void recipient_zero(void)
{
    setup(&frame_app); solar_os_sessions_touch_unbind(1);
    assert(solar_os_sessions_touch_bind(0,"display0",&frame_app,&alive,receive)==ESP_OK);
    arm(); point(0,3,20,30); assert(send(1)==ESP_OK && dispatch(0));
    assert(seen.data.touch_frame->recipient==0 && frames==1);
}
#ifdef INPUT_TOUCH_SERVICE_FIXTURE
int service_fixture_main(void)
#else
int main(void)
#endif
{
#define RUN(f) do { f(); ++cases; } while(0)
    RUN(normal_frames); RUN(initial_gate); RUN(overflow_and_gate); RUN(cancellation_ack_gate);
    RUN(invalid_and_failure); RUN(sequences); RUN(source_reopen); RUN(recipient_reuse);
    RUN(switch_during_callback); RUN(focus_and_suspend); RUN(legacy_projection);
    RUN(legacy_secondary_first); RUN(legacy_cancel); RUN(rotations); RUN(removed_recipient);
    RUN(acquired_before_switch); RUN(local_batch_stale); RUN(monotonic_contract);
    RUN(calibration_and_double_delivery); RUN(target_handoff); RUN(source_ready_and_multi_source);
    RUN(capacities_and_wrap);
    RUN(legacy_source_isolation); RUN(legacy_reordered_ids);
    RUN(sequence_exhaustion);
    RUN(recipient_zero);
    solar_os_sessions_touch_unbind_all(); solar_os_input_source_close(source);
    printf("input_touch_service_test: %u cases PASS; event=%zu touch=%zu pointer=%zu\n",
        cases,sizeof(solar_os_event_t),sizeof(solar_os_input_touch_frame_t),sizeof(solar_os_input_pointer_event_t));
    printf("touch storage: queue=%zu source-state=%zu consumers=%zu\n",sizeof(input_touch_queue),sizeof(input_touch_sources),sizeof(input_touch_consumers));
    return 0;
}
