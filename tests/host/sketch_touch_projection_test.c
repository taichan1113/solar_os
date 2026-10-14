/* Actual Sketch fixture and actual projection; transport metadata is stubbed.
 * Queue/epoch/cancellation admission are tested separately with real Input. */
#define main sketch_fixture_main
#include "sketch_lifecycle_test.c"
#undef main
#include "solar_os_sessions_touch.h"
static uint64_t generation;
esp_err_t solar_os_input_touch_consumer_register(uint8_t owner,const char *target,uint64_t *token)
{ assert(owner==1 && !strcmp(target,"display0")); *token=++generation; return ESP_OK; }
void solar_os_input_touch_consumer_cancel(uint64_t token) { assert(token==generation); }
void solar_os_input_touch_consumer_unregister(uint64_t token) { assert(token==generation); }
bool solar_os_input_read_touch_cancel(uint64_t token,solar_os_input_touch_frame_t *frame)
{ (void)token; (void)frame; return false; }
bool solar_os_input_touch_frame_valid(const solar_os_input_touch_frame_t *frame)
{ return frame->generation==generation && frame->source==1; }
void solar_os_input_touch_finish(const solar_os_input_touch_frame_t *frame,bool delivered)
{ (void)frame; (void)delivered; }
esp_err_t solar_os_input_touch_apply_orientation(solar_os_input_touch_frame_t *frame,uint16_t w,uint16_t h,uint16_t degrees)
{ assert(frame && w==800 && h==480 && degrees==0); return ESP_OK; }
static unsigned pointer_callbacks, cancel_callbacks;
static bool receive_sketch(void *identity,const solar_os_event_t *event)
{
    solar_os_context_t *ctx=identity;
    if(event->type==SOLAR_OS_EVENT_TOUCH_CANCEL) {
        ++cancel_callbacks;
        /* The actual Session adapter uses this same balanced legacy cleanup. */
        solar_os_sketch_app.suspend(ctx); solar_os_sketch_app.resume(ctx);
    } else {
        assert(event->type==SOLAR_OS_EVENT_POINTER); ++pointer_callbacks;
        solar_os_sketch_app.event(ctx,event);
    }
    return true;
}
int main(void)
{
    solar_os_context_t ctx=begin(0); assert(sketch_start(&ctx)==ESP_OK);
    assert(solar_os_sessions_touch_bind(1,"display0",&solar_os_sketch_app,&ctx,receive_sketch)==ESP_OK);
    solar_os_input_touch_frame_t f={.source=1,.recipient=1,.generation=generation,.epoch=1};
    strcpy(f.target,"display0"); f.snapshot.count=1;
    f.snapshot.contacts[0]=(solar_os_touch_contact_t){3,100,100};
    assert(solar_os_sessions_touch_dispatch(&f,800,480,0) && sketch.drawing);
    f.snapshot.count=2; f.snapshot.contacts[1]=(solar_os_touch_contact_t){7,200,200};
    assert(solar_os_sessions_touch_dispatch(&f,800,480,0) && pointer_callbacks==1);
    f.snapshot.contacts[0].x=130;
    assert(solar_os_sessions_touch_dispatch(&f,800,480,0) && pointer_callbacks==2);
    sketch_layout_t layout=sketch_layout(ctx.gfx);
    assert(solar_os_sketch_canvas_get(&sketch.canvas,130-layout.canvas_x,100-layout.canvas_y)==3);
    f.snapshot.count=1; f.snapshot.contacts[0]=(solar_os_touch_contact_t){7,200,200};
    assert(solar_os_sessions_touch_dispatch(&f,800,480,0) && !sketch.drawing && pointer_callbacks==3);
    assert(solar_os_sessions_touch_dispatch(&f,800,480,0) && pointer_callbacks==3);
    f.snapshot.count=0; solar_os_sessions_touch_dispatch(&f,800,480,0);
    f.snapshot.count=1; f.snapshot.contacts[0]=(solar_os_touch_contact_t){7,140,110};
    assert(solar_os_sessions_touch_dispatch(&f,800,480,0) && sketch.drawing);
    unsigned previous=pointer_callbacks; f.state=SOLAR_OS_INPUT_TOUCH_CANCEL; f.snapshot.count=0;
    assert(solar_os_sessions_touch_dispatch(&f,800,480,0) && !sketch.drawing && cancel_callbacks==1);
    assert(pointer_callbacks==previous); /* No synthetic RELEASE, Line/Rect commit. */
    assert(solar_os_sketch_canvas_get(&sketch.canvas,130-layout.canvas_x,100-layout.canvas_y)==3);
    solar_os_sessions_touch_unbind(1); end(&ctx);
    puts("sketch_touch_projection_test: PASS (actual projection + Sketch stroke/cancel)");
    return 0;
}
