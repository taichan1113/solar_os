#define main unused_launcher_main
#include "launcher_touch_test.c"
#undef main
#include "../../src/apps/solar_os_clock.c"
static unsigned updates;static int64_t now;
static solar_os_datetime_t rtc={.year=2026,.month=10,.day=19,.hour=12,.minute=34,.clock_integrity=true};
int64_t esp_timer_get_time(void) { return now; }
esp_err_t solar_os_time_get_datetime(solar_os_datetime_t *dt) { *dt=rtc;return ESP_OK; }
bool solar_os_time_datetime_is_valid(const solar_os_datetime_t *dt) { return dt->year>2000; }
void solar_os_gfx_fill_polygon(solar_os_gfx_t *g,const solar_os_gfx_point_t *p,size_t n) { (void)g;(void)p;(void)n; }
esp_err_t solar_os_schedule_remaining_seconds(const char *n,uint32_t *s) { (void)n;*s=10;return ESP_OK; }
esp_err_t solar_os_schedule_add_relative(const char *n,uint32_t s,solar_os_schedule_action_t a,const char *v,bool p)
{ (void)n;(void)s;(void)a;(void)v;(void)p;return ESP_OK; }
esp_err_t solar_os_schedule_remove(const char *n) { (void)n;return ESP_OK; }
bool solar_os_schedule_alarm_active(char *n,size_t s) { (void)n;(void)s;return false; }
void solar_os_schedule_stop_alarm(void) { }
int main(void)
{
    clock_state_t state={0}; clock_state_storage=&state;
    solar_os_context_t ctx={.gfx=&display};
    clock_render(&ctx);uint32_t minute=state.last_second;
    unsigned initial_presents=gfx_presents;
    for (unsigned i=0;i<59;i++) {
        rtc.second=i;clock_event(&ctx,&(solar_os_event_t){.type=SOLAR_OS_EVENT_TICK,.data.tick_ms=i*1000});
        assert(state.last_second==minute && gfx_presents==initial_presents);
    }
    rtc.minute++;clock_event(&ctx,&(solar_os_event_t){.type=SOLAR_OS_EVENT_TICK,.data.tick_ms=60000});
    assert(state.last_second!=minute && gfx_presents==initial_presents+1);
    clock_event(&ctx,&(solar_os_event_t){.type=SOLAR_OS_EVENT_CHAR,.data.ch=SOLAR_OS_KEY_ESCAPE});
    assert(ctx.exit_requested);
    state.mode=CLOCK_MODE_STOPWATCH;
    solar_os_input_pointer_event_t p={.mode=SOLAR_OS_INPUT_POINTER_ABSOLUTE,.action=SOLAR_OS_INPUT_POINTER_PRESS,.x=300,.y=200};
    clock_event(&ctx,&(solar_os_event_t){.type=SOLAR_OS_EVENT_POINTER,.data.pointer=p});
    p.action=SOLAR_OS_INPUT_POINTER_RELEASE;
    clock_event(&ctx,&(solar_os_event_t){.type=SOLAR_OS_EVENT_POINTER,.data.pointer=p});
    assert(state.stopwatch_running);
    (void)updates;
    puts("clock_touch_test: RTC minute/date signature, Esc, existing stopwatch tap PASS");
}
