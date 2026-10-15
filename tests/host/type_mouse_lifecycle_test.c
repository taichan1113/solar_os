/* Exercise actual app callbacks and engine with disconnected BLE peripherals. */
#include <assert.h>
#include "../../src/apps/solar_os_type_mouse.c"
struct solar_os_gfx { size_t width, height; };
static struct solar_os_gfx display={800,480};
static bool leased, online;
static unsigned renders, keyboard_releases, mouse_releases, closes;
solar_os_gfx_t *solar_os_context_gfx(solar_os_context_t *c) { return c->gfx; }
void solar_os_context_set_graphics_active(solar_os_context_t *c,bool v) { c->graphics_active=v; }
void solar_os_context_finish(solar_os_context_t *c,int e,const char *s) { (void)s; c->exit_requested=true; c->exit_code=e; }
void solar_os_gfx_clear(solar_os_gfx_t *g,solar_os_gfx_color_t c) { (void)g; (void)c; }
void solar_os_gfx_set_color(solar_os_gfx_t *g,solar_os_gfx_color_t c) { (void)g; (void)c; }
void solar_os_gfx_set_font_native(solar_os_gfx_t *g,solar_os_gfx_font_t f) { (void)g; (void)f; }
void solar_os_gfx_text(solar_os_gfx_t *g,int x,int y,const char *s) { (void)g; (void)x; (void)y; (void)s; }
void solar_os_gfx_rect(solar_os_gfx_t *g,int x,int y,int w,int h) { (void)g; (void)x; (void)y; (void)w; (void)h; }
esp_err_t solar_os_gfx_submit(solar_os_gfx_t *g,uint64_t *s) { (void)g; (void)s; ++renders; return ESP_OK; }
esp_err_t solar_os_expansion_enable_acquire(const char *n,const void *p)
{ assert(!strcmp(n,"fixture-pointer") && p==type_mouse_storage && !leased); leased=true; return ESP_OK; }
esp_err_t solar_os_expansion_enable_release(const char *n,const void *p)
{ assert(!strcmp(n,"fixture-pointer") && p==type_mouse_storage && leased); leased=false; return ESP_OK; }
esp_err_t solar_os_ble_session_create(const char *n,solar_os_ble_session_t *s)
{ assert(!strcmp(n,"type_mouse")); *s=1; return ESP_OK; }
esp_err_t solar_os_ble_session_close(solar_os_ble_session_t s) { assert(s==1); ++closes; return ESP_OK; }
esp_err_t solar_os_ble_hid_device_start(solar_os_ble_session_t s,const char *n)
{ assert(s==1 && !strcmp(n,"SolarOS TypeMouse")); return ESP_OK; }
esp_err_t solar_os_ble_hid_device_stop(solar_os_ble_session_t s) { assert(s==1); return ESP_OK; }
esp_err_t solar_os_ble_hid_device_pair(solar_os_ble_session_t s) { (void)s; assert(false); return ESP_FAIL; }
esp_err_t solar_os_ble_hid_device_status(solar_os_ble_session_t s,solar_os_ble_hid_info_t *i)
{ assert(s==1); *i=(solar_os_ble_hid_info_t){.connected=online,.encrypted=online,.keyboard_subscribed=online,.mouse_subscribed=online}; return ESP_OK; }
esp_err_t solar_os_ble_hid_device_poll(solar_os_ble_session_t s,solar_os_ble_hid_device_event_t *e)
{ (void)s; (void)e; return ESP_ERR_NOT_FOUND; }
esp_err_t solar_os_ble_hid_keyboard_release_all(solar_os_ble_session_t s)
{ assert(s==1); ++keyboard_releases; return ESP_OK; }
esp_err_t solar_os_ble_hid_keyboard_press(solar_os_ble_session_t s,const uint16_t *k,size_t n)
{ (void)s; (void)k; (void)n; return ESP_OK; }
esp_err_t solar_os_ble_hid_keyboard_release(solar_os_ble_session_t s,const uint16_t *k,size_t n)
{ (void)s; (void)k; (void)n; return ESP_OK; }
esp_err_t solar_os_ble_hid_mouse_report(solar_os_ble_session_t s,uint8_t b,int16_t x,int16_t y,int16_t w,int16_t p)
{ assert(s==1); if(!b && !x && !y && !w && !p) ++mouse_releases; return ESP_OK; }
static void exit_touch(solar_os_context_t *c,bool cancel)
{
    solar_os_input_touch_frame_t f={.state=SOLAR_OS_INPUT_TOUCH_SAMPLE};
    f.snapshot.count=1; f.snapshot.contacts[0]=(solar_os_touch_contact_t){.id=3,.x=710,.y=30};
    type_mouse_frame(c,&f); assert(tm.header_armed && !c->exit_requested);
    f.snapshot.count=0; if(cancel) f.state=SOLAR_OS_INPUT_TOUCH_CANCEL;
    type_mouse_frame(c,&f);
    assert(c->exit_requested==!cancel);
}
int main(void)
{
    type_mouse_state_t state; type_mouse_storage=&state;
    solar_os_context_t c={.gfx=&display};
    assert(type_mouse_start(&c)==ESP_OK && leased && renders==1 && c.graphics_active);
    assert(!tm.connected); type_mouse_status(&c,200); assert(!tm.connected);
    exit_touch(&c,true); assert(!tm.header_armed);
    exit_touch(&c,false); assert(c.exit_code==0);
    type_mouse_stop(&c);
    assert(!leased && !tm.engine && !c.graphics_active && closes==1);
    c.exit_requested=false;
    assert(type_mouse_start(&c)==ESP_OK);
    online=true; type_mouse_status(&c,200);
    assert(tm.connected && tm.keyboard_ready && tm.mouse_ready);
    tm.last_modifiers=2; tm.last_keys[0]=4; tm.mouse_buttons=1; tm.header_armed=true;
    unsigned kr=keyboard_releases,mr=mouse_releases;
    type_mouse_suspend(&c);
    assert(!leased && !tm.header_armed && !tm.last_modifiers && !tm.last_keys[0] && !tm.mouse_buttons);
    assert(keyboard_releases>kr && mouse_releases>mr);
    type_mouse_resume(&c); assert(leased && !tm.suspended);
    solar_os_input_touch_frame_t release={.state=SOLAR_OS_INPUT_TOUCH_SAMPLE};
    type_mouse_frame(&c,&release); assert(!c.exit_requested);
    exit_touch(&c,false); type_mouse_stop(&c); type_mouse_cleanup();
    assert(closes==2 && !leased && !tm.engine);
    puts("type_mouse_lifecycle_test: offline start/touch Exit, CANCEL, suspend/resume and HID release PASS");
    return 0;
}
