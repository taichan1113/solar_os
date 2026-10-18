#define SOLAR_OS_BOARD_UI_SCALE_PERCENT 140
#include <assert.h>
#include "../../src/apps/solar_os_reader.c"
unsigned solar_os_ui_scale_percent(void) { return 140; }
static unsigned header_texts;
size_t solar_os_gfx_width(const solar_os_gfx_t *g) { (void)g; return 800; }
void solar_os_gfx_set_color(solar_os_gfx_t *g,solar_os_gfx_color_t c) { (void)g;(void)c; }
void solar_os_gfx_fill_rect(solar_os_gfx_t *g,int x,int y,int w,int h)
{ (void)g;(void)x;(void)y;(void)w;(void)h; }
void solar_os_gfx_set_font(solar_os_gfx_t *g,solar_os_gfx_font_t f) { (void)g;(void)f; }
size_t solar_os_gfx_text_fit(solar_os_gfx_t *g,const char *s,int w,char *out,size_t size)
{ (void)g; assert(w==719 && size);size_t n=strlen(s);if(n>=size)n=size-1;memcpy(out,s,n);out[n]=0;return n; }
void solar_os_gfx_text(solar_os_gfx_t *g,int x,int y,const char *s)
{ (void)g;(void)y;assert(x==4 || x>=78);if (x==4) assert(!strcmp(s,"Files"));header_texts++; }
int main(void)
{
    reader_state_t state={0};reader_state=&state;
    reader_draw_header((solar_os_gfx_t *)&state);assert(header_texts==2);
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
