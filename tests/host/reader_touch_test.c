#define SOLAR_OS_BOARD_UI_SCALE_PERCENT 140
#include <assert.h>
#include "../../src/apps/solar_os_reader.c"
unsigned solar_os_ui_scale_percent(void) { return 140; }
int main(void)
{
    solar_os_app_touch_t t={.x=300,.y=200};
    solar_os_input_pointer_event_t p={.x=304,.y=204};
    assert(reader_touch_action(&t,&p,800)==SOLAR_OS_KEY_PAGE_UP);
    p.x=600; assert(reader_touch_action(&t,&p,800)==SOLAR_OS_KEY_PAGE_UP); /* deliberate right swipe */
    t.moved=true; p.x=320;p.y=220;assert(reader_touch_action(&t,&p,800)==0);
    p.x=240;assert(reader_touch_action(&t,&p,800)==SOLAR_OS_KEY_PAGE_DOWN);
    p.x=300;p.y=260;assert(reader_touch_action(&t,&p,800)==SOLAR_OS_KEY_PAGE_UP);
    t=(solar_os_app_touch_t){.x=600,.y=200};p=(solar_os_input_pointer_event_t){.x=600,.y=200};
    assert(reader_touch_action(&t,&p,800)==SOLAR_OS_KEY_PAGE_DOWN);
    t=(solar_os_app_touch_t){.x=10,.y=10};p=(solar_os_input_pointer_event_t){.x=10,.y=10};
    assert(reader_touch_action(&t,&p,800)=='o');
    puts("reader_touch_test: left/right tap, swipe, jitter deadband, Files target PASS");
}
