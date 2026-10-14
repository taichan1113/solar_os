#define INPUT_TOUCH_SERVICE_FIXTURE
#include "input_touch_service_test.c"
#include "solar_os_display.h"
#include "solar_os_terminal.h"

#define SOLAR_OS_SESSION_MAX 8U
typedef struct {
    bool used, started, suspended;
    uint8_t id;
    uint64_t touch_lifetime;
    const solar_os_app_t *app;
    char display_target[16];
} solar_os_session_entry_t;
typedef struct { unsigned owner; } solar_os_session_context_snapshot_t;
static struct {
    solar_os_context_t *ctx;
    solar_os_session_entry_t sessions[8];
    solar_os_session_entry_t *foreground_session;
    const solar_os_app_t *foreground_app;
    uint64_t legacy_touch_lifetime;
    char builtin_display_target[16];
} session_state;
static unsigned current_owner, adapter_calls, adapter_cancels, adapter_suspends, adapter_resumes;
static bool close_on_cancel;
static bool reuse_on_cancel;
static uint16_t display_rotation;
static solar_os_input_touch_frame_t adapter_copy;
static void session_context_capture(solar_os_session_context_snapshot_t *s) { s->owner=current_owner; }
static void session_context_restore(const solar_os_session_context_snapshot_t *s) { current_owner=s->owner; }
static void session_prepare_context(solar_os_session_entry_t *s) { current_owner=s->id; }
static void restore_foreground_context(void) { current_owner=session_state.foreground_session->id; }
static void session_restore_base_context(void) { current_owner=0; }
static bool session_copy_input_focus(char *target,size_t size) { strlcpy(target,"display0",size); return true; }
static bool adapter_event(solar_os_context_t *ctx,const solar_os_event_t *e)
{
    assert(ctx==session_state.ctx && current_owner==1 && session_state.sessions[1].used);
    ++adapter_calls;
    if(e->type==SOLAR_OS_EVENT_TOUCH_CANCEL ||
        (e->type==SOLAR_OS_EVENT_TOUCH_FRAME && e->data.touch_frame->state==SOLAR_OS_INPUT_TOUCH_CANCEL)) {
        ++adapter_cancels;
        if(close_on_cancel) {
            session_state.sessions[1].used=false;
            session_state.foreground_session=NULL; session_state.foreground_app=NULL;
        }
        if(reuse_on_cancel) ++session_state.sessions[1].touch_lifetime;
    }
    if(e->type==SOLAR_OS_EVENT_TOUCH_FRAME) adapter_copy=*e->data.touch_frame;
    return true;
}
static void adapter_suspend(solar_os_context_t *ctx) { assert(ctx==session_state.ctx); ++adapter_suspends; }
static void adapter_resume(solar_os_context_t *ctx) { assert(ctx==session_state.ctx); ++adapter_resumes; }
static const solar_os_app_t adapter_frame_app={.name="adapter-frame",.flags=SOLAR_OS_APP_FLAG_TOUCH_FRAMES,.event=adapter_event};
static const solar_os_app_t adapter_pointer_app={.name="adapter-pointer",.flags=SOLAR_OS_APP_FLAG_POINTER_EVENTS,
    .event=adapter_event,.suspend=adapter_suspend,.resume=adapter_resume};
bool solar_os_sessions_dispatch_session_event(uint8_t id,const solar_os_event_t *e)
{
    solar_os_session_entry_t *s=&session_state.sessions[id];
    if(!s->used || !s->started || s->suspended) return false;
    unsigned prior=current_owner; current_owner=id;
    bool ok=s->app->event(session_state.ctx,e); current_owner=prior; return ok;
}
void solar_os_sessions_dispatch_foreground_event(const solar_os_event_t *e)
{ assert(session_state.foreground_app); session_state.foreground_app->event(session_state.ctx,e); }
bool solar_os_display_find_target(const char *name,solar_os_display_target_t *target)
{ assert(!strcmp(name,"display0")); memset(target,0,sizeof(*target)); target->width=480; target->height=800; return true; }
esp_err_t solar_os_display_get_terminal_profile(const char *name,solar_os_terminal_profile_t *profile)
{ assert(!strcmp(name,"display0")); memset(profile,0,sizeof(*profile)); profile->orientation_degrees=display_rotation; return ESP_OK; }

#include "../../src/services/solar_os_sessions_touch_adapter.inc"

static solar_os_context_t adapter_context;
static void adapter_setup(const solar_os_app_t *app)
{
    setup(&frame_app); solar_os_sessions_touch_unbind_all();
    memset(&session_state,0,sizeof(session_state));
    session_state.ctx=&adapter_context;
    session_state.sessions[1]=(solar_os_session_entry_t){.id=1,.used=true,.started=true,.app=app,.touch_lifetime=1};
    strcpy(session_state.sessions[1].display_target,"display0");
    session_state.foreground_session=&session_state.sessions[1]; session_state.foreground_app=app;
    strcpy(session_state.builtin_display_target,"display0");
    current_owner=0; adapter_calls=adapter_cancels=adapter_suspends=adapter_resumes=0;
    close_on_cancel=reuse_on_cancel=false; display_rotation=0;
    solar_os_sessions_refresh_touch_consumers();
}
static bool adapter_dispatch(void)
{
    solar_os_input_touch_frame_t f;
    if(!solar_os_input_read_touch_frame(&f)) return false;
    bool delivered=solar_os_sessions_dispatch_touch_frame(&f);
    solar_os_input_touch_finish(&f,delivered); return true;
}
static void adapter_arm(void) { assert(send(0)==ESP_OK && adapter_dispatch()); adapter_calls=0; }
static void live_routing(void)
{
    adapter_setup(&adapter_frame_app); adapter_arm(); point(0,3,20,30); point(1,7,40,50);
    send(2); assert(adapter_dispatch() && adapter_calls==1 && adapter_copy.snapshot.count==2 && current_owner==0);
}
static void retire_before_stop(void)
{
    adapter_setup(&adapter_frame_app); adapter_arm(); point(0,3,20,30); send(1); adapter_dispatch();
    solar_os_sessions_quiesce_touch_consumers(); assert(adapter_cancels==1 && current_owner==0);
    session_state.sessions[1].used=false; alive=false; assert(!adapter_dispatch());
}
static void legacy_state_release(void)
{
    adapter_setup(&adapter_pointer_app); adapter_arm(); point(0,3,20,30); send(1); adapter_dispatch();
    solar_os_input_touch_consumer_cancel(ticket.generation); assert(adapter_dispatch());
    assert(adapter_cancels==1 && adapter_suspends==1 && adapter_resumes==1 && current_owner==0);
}
static void stopped_inside_cancel(void)
{
    adapter_setup(&adapter_pointer_app); adapter_arm(); point(0,3,20,30); send(1); adapter_dispatch();
    close_on_cancel=true; solar_os_sessions_quiesce_touch_consumers();
    assert(adapter_cancels==1 && !adapter_suspends && !adapter_resumes && !session_state.sessions[1].used && current_owner==0);
}
static void suspend_resume_generation(void)
{
    adapter_setup(&adapter_frame_app); adapter_arm(); point(0,3,20,30); send(1); adapter_dispatch();
    uint64_t previous=ticket.generation;
    solar_os_sessions_quiesce_touch_consumers(); session_state.sessions[1].suspended=true;
    solar_os_sessions_refresh_touch_consumers();
    assert(solar_os_input_touch_capture(source,"display0",&ticket)==ESP_ERR_NOT_FOUND);
    session_state.sessions[1].suspended=false; solar_os_sessions_refresh_touch_consumers();
    assert(send(1)==ESP_OK && ticket.generation!=previous && !adapter_dispatch());
    adapter_arm(); send(1); assert(adapter_dispatch());
}
static void adapter_orientation(void)
{
    adapter_setup(&adapter_frame_app); adapter_arm(); display_rotation=270;
    point(0,3,20,30); point(1,7,40,50); send(2); adapter_dispatch();
    assert(adapter_copy.snapshot.contacts[0].x==769 && adapter_copy.snapshot.contacts[0].y==20);
    assert(adapter_copy.snapshot.contacts[1].x==749 && adapter_copy.snapshot.contacts[1].y==40);
}
static void same_slot_reused_during_cancel(void)
{
    adapter_setup(&adapter_pointer_app); adapter_arm(); point(0,3,20,30); send(1); adapter_dispatch();
    reuse_on_cancel=true; solar_os_sessions_quiesce_touch_consumers();
    assert(adapter_cancels==1 && !adapter_suspends && !adapter_resumes && current_owner==1);
}
int main(void)
{
    cases=0;
    RUN(live_routing); RUN(retire_before_stop); RUN(legacy_state_release); RUN(stopped_inside_cancel);
    RUN(suspend_resume_generation); RUN(adapter_orientation);
    RUN(same_slot_reused_during_cancel);
    solar_os_sessions_quiesce_touch_consumers(); solar_os_input_source_close(source);
    printf("input_touch_adapter_test: %u cases PASS (actual Session adapter)\n",cases);
}
