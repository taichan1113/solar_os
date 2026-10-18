#define SOLAR_OS_BOARD_LAUNCHER_TOUCH_GRID 1
#include <assert.h>
#include <stdio.h>
#include "solar_os.h"
#include "solar_os_app_touch.h"
static unsigned acquire_count,release_count;
void vTaskDelay(TickType_t t) { (void)t; }
esp_err_t solar_os_expansion_enable_acquire(const char *n,const void *p)
{ (void)n; assert(p); acquire_count++; return ESP_OK; }
esp_err_t solar_os_expansion_enable_release(const char *n,const void *p)
{ (void)n; assert(p); release_count++; return ESP_OK; }
int main(void)
{
    solar_os_app_touch_t t={0};
    assert(solar_os_app_touch_acquire(&t)==ESP_OK && t.leased);
    solar_os_input_pointer_event_t p={.mode=SOLAR_OS_INPUT_POINTER_ABSOLUTE,
       .action=SOLAR_OS_INPUT_POINTER_PRESS,.x=100,.y=100};
    assert(!solar_os_app_touch_end(&t,&p));
    p.action=SOLAR_OS_INPUT_POINTER_MOVE;p.x+=4;assert(!solar_os_app_touch_end(&t,&p) && !t.moved);
    solar_os_app_touch_cancel(&t);solar_os_app_touch_suspend(&t);
    assert(t.leased && release_count==0 && !solar_os_app_touch_resume(&t));
    p.action=SOLAR_OS_INPUT_POINTER_RELEASE;assert(!solar_os_app_touch_end(&t,&p));
    for (unsigned i=0;i<5;i++) {
        solar_os_app_touch_suspend(&t);assert(!t.leased && !t.pressed);
        assert(solar_os_app_touch_resume(&t));assert(solar_os_app_touch_acquire(&t)==ESP_OK);
        p.action=SOLAR_OS_INPUT_POINTER_PRESS;assert(!solar_os_app_touch_end(&t,&p));
        p.action=SOLAR_OS_INPUT_POINTER_MOVE;p.y+=60;assert(!solar_os_app_touch_end(&t,&p) && t.moved);
        p.action=SOLAR_OS_INPUT_POINTER_RELEASE;assert(solar_os_app_touch_end(&t,&p));
        assert(!solar_os_app_touch_end(&t,&p));
    }
    solar_os_app_touch_release(&t);assert(acquire_count==release_count);
    puts("app_touch_test: lease/CANCEL, no duplicate release, jitter, swipe, five lifecycle cycles PASS");
}
