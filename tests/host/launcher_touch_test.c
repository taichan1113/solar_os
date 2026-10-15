/* Actual Launcher configuration overlay, leases and pointer activation. */
#include <assert.h>
#include <stdlib.h>
#include "../../src/apps/solar_os_launcher.c"

struct solar_os_gfx { size_t width, height; };
static struct solar_os_gfx display = {800, 480};
static bool leased, fail_enable;
static unsigned acquires, releases, delays;
static solar_os_launch_policy_t launch_policy;
static unsigned writes;
static const char *config_file="launcher-touch-fixture.json";
const solar_os_app_t solar_os_type_mouse_app = {.name = "type-mouse"};
static const solar_os_app_registry_entry_t child_entry = {
    .name = "typemouse", .app = &solar_os_type_mouse_app, .min_argc = 1, .max_argc = 1};

void vTaskDelay(TickType_t ticks) { assert(ticks == 100); ++delays; }

size_t strlcpy(char *d, const char *s, size_t n)
{ size_t l = strlen(s); if (n) { size_t c = l < n-1 ? l : n-1; memcpy(d,s,c); d[c]=0; } return l; }
const char *esp_err_to_name(esp_err_t e) { (void)e; return "fixture error"; }
void *solar_os_memory_alloc(size_t n,solar_os_memory_class_t c,const char *t) { (void)c; (void)t; return malloc(n); }
void solar_os_memory_free(void *p) { free(p); }
int solar_os_context_argc(const solar_os_context_t *c) { return c->argc; }
const char *solar_os_context_argv(const solar_os_context_t *c,int i) { return c->argv[i]; }
esp_err_t solar_os_storage_resolve_path(const char *p,char *o,size_t n) { return strlcpy(o,p,n)<n ? ESP_OK : ESP_ERR_INVALID_SIZE; }
esp_err_t solar_os_storage_default_path(const char *p,char *o,size_t n) { (void)p; return solar_os_storage_resolve_path(config_file,o,n); }
esp_err_t solar_os_storage_sibling_path(const char *p,const char *s,char *o,size_t n) { return snprintf(o,n,"%s%s",p,s)<(int)n ? ESP_OK : ESP_ERR_INVALID_SIZE; }
esp_err_t solar_os_storage_remove(const char *p) { return remove(p)==0 ? ESP_OK : ESP_FAIL; }
esp_err_t solar_os_storage_sync_file(FILE *f) { ++writes; return fflush(f)==0 ? ESP_OK : ESP_FAIL; }
esp_err_t solar_os_storage_replace_file(const char *t,const char *p,const char *b) { (void)b; ++writes; return rename(t,p)==0 ? ESP_OK : ESP_FAIL; }
esp_err_t solar_os_storage_read_file(const char *p,void *o,size_t n,size_t *l) { FILE *f=fopen(p,"rb"); if(!f) return ESP_FAIL; *l=fread(o,1,n,f); fclose(f); return ESP_OK; }
esp_err_t solar_os_expansion_enable_acquire(const char *n, const void *owner)
{ assert(!strcmp(n,"fixture-pointer") && owner == launcher_state_storage); ++acquires; if(fail_enable) return ESP_FAIL; leased=true; return ESP_OK; }
esp_err_t solar_os_expansion_enable_release(const char *n, const void *owner)
{ assert(!strcmp(n,"fixture-pointer") && owner == launcher_state_storage && leased); ++releases; leased=false; return ESP_OK; }
solar_os_gfx_t *solar_os_context_gfx(solar_os_context_t *c) { return c->gfx; }
void solar_os_context_set_graphics_active(solar_os_context_t *c, bool v) { c->graphics_active=v; }
void solar_os_context_finish(solar_os_context_t *c,int e,const char *s)
{ (void)s; c->exit_requested=true; c->exit_code=e; }
size_t solar_os_gfx_width(const solar_os_gfx_t *g) { return g->width; }
size_t solar_os_gfx_height(const solar_os_gfx_t *g) { return g->height; }
size_t solar_os_gfx_text_width(solar_os_gfx_t *g,const char *s) { (void)g; return strlen(s)*8; }
void solar_os_gfx_clear(solar_os_gfx_t *g,solar_os_gfx_color_t c) { (void)g; (void)c; }
void solar_os_gfx_set_color(solar_os_gfx_t *g,solar_os_gfx_color_t c) { (void)g; (void)c; }
void solar_os_gfx_set_font(solar_os_gfx_t *g,solar_os_gfx_font_t f) { (void)g; (void)f; }
void solar_os_gfx_set_line_style(solar_os_gfx_t *g,solar_os_gfx_line_style_t s) { (void)g; (void)s; }
void solar_os_gfx_line(solar_os_gfx_t *g,int x,int y,int a,int b) { (void)g; (void)x; (void)y; (void)a; (void)b; }
void solar_os_gfx_fill_rect(solar_os_gfx_t *g,int x,int y,int w,int h) { (void)g; (void)x; (void)y; (void)w; (void)h; }
void solar_os_gfx_circle(solar_os_gfx_t *g,int x,int y,int r) { (void)g; (void)x; (void)y; (void)r; }
void solar_os_gfx_fill_circle(solar_os_gfx_t *g,int x,int y,int r) { solar_os_gfx_circle(g,x,y,r); }
void solar_os_gfx_icon(solar_os_gfx_t *g,int x,int y,solar_os_gfx_icon_t i,solar_os_gfx_icon_size_t z) { (void)g; (void)x; (void)y; (void)i; (void)z; }
void solar_os_gfx_text(solar_os_gfx_t *g,int x,int y,const char *s) { (void)g; (void)x; (void)y; (void)s; }
void solar_os_gfx_present(solar_os_gfx_t *g) { (void)g; }
const solar_os_app_registry_entry_t *solar_os_app_registry_find(const char *n)
{ return !strcmp(n,"typemouse") ? &child_entry : NULL; }
esp_err_t solar_os_context_request_launch_ex(solar_os_context_t *c,const solar_os_app_t *a,int n,char **v,solar_os_launch_policy_t p)
{ assert(n==1 && !strcmp(v[0],"typemouse") && p==SOLAR_OS_LAUNCH_CHILD_RETURN); c->requested_app=a; launch_policy=p; return ESP_OK; }
esp_err_t solar_os_shell_resolve_path(solar_os_context_t *c,const char *p,char *o,size_t n)
{ (void)c; return strlcpy(o,p,n)<n ? ESP_OK : ESP_ERR_INVALID_SIZE; }
esp_err_t solar_os_shell_execute_command(solar_os_context_t *c,const char *s)
{ (void)c; (void)s; assert(false); return ESP_FAIL; }
/* Unused script branch must never be taken by a TypeMouse launch. */
bool solar_os_shell_run_script(solar_os_context_t *c,const char *p,const char *s,bool b)
{ (void)c; (void)p; (void)s; (void)b; assert(false); return 1; }

static void configuration_overlay(void)
{
    for (unsigned rows=1; rows<=8; ++rows) for(unsigned columns=1; columns<=8; ++columns) {
        memset(&launcher.config,0,sizeof(launcher.config));
        launcher.config.rows=rows; launcher.config.columns=columns;
        size_t count=rows*columns; if(count>32) count=32;
        launcher.config.item_count=count;
        for(size_t i=0;i<count;++i) {
            launcher.config.items[i].cell=(solar_os_launcher_cell_t){i%columns,i/columns};
            strlcpy(launcher.config.items[i].command,"writer",LAUNCHER_COMMAND_MAX);
        }
        launcher_config_t before=launcher.config;
        launcher_add_type_mouse();
        assert(launcher.config.item_count==count+1);
        assert(!memcmp(before.items,launcher.config.items,count*sizeof(launcher_item_t)));
        assert(launcher.config.rows<=8 && launcher.config.columns<=8);
        solar_os_launcher_cell_t cells[LAUNCHER_ITEM_MAX]; launcher_copy_cells(cells);
        assert(solar_os_launcher_layout_valid(cells,count+1,launcher.config.columns,launcher.config.rows));
        launcher_item_t *item=&launcher.config.items[count];
        assert(!strcmp(item->name,"TypeMouse") && !strcmp(item->command,"typemouse") && item->icon==SOLAR_OS_GFX_ICON_KEYBOARD);
        launcher_add_type_mouse(); assert(launcher.config.item_count==count+1);
    }
    /* Existing quoted commands count as TypeMouse; preserve title and position. */
    launcher.config.item_count=1;
    strlcpy(launcher.config.items[0].command,"  \"typemouse\"  ",LAUNCHER_COMMAND_MAX);
    strlcpy(launcher.config.items[0].name,"My keyboard",LAUNCHER_NAME_MAX);
    launcher.config.items[0].icon=SOLAR_OS_GFX_ICON_KEY;
    launcher_add_type_mouse();
    assert(launcher.config.item_count==1 && !strcmp(launcher.config.items[0].name,"My keyboard"));
    assert(launcher.config.items[0].icon==SOLAR_OS_GFX_ICON_KEYBOARD);
}

int main(void)
{
    launcher_state_t state={0}; launcher_state_storage=&state;
    configuration_overlay();
    solar_os_context_t ctx={.gfx=&display};
    fail_enable=true; assert(launcher_touch_acquire()==ESP_FAIL && !launcher.touch_lease);
    assert(acquires==3 && delays==2);
    launcher_touch_release(); assert(releases==0);
    fail_enable=false; assert(launcher_touch_acquire()==ESP_OK && leased);
    assert(launcher_touch_acquire()==ESP_OK && acquires==4);
    launcher.pointer_relative=true; launcher_suspend(&ctx);
    assert(!leased && !launcher.touch_lease && !launcher.pointer_relative && launcher.suspended);
    assert(launcher_touch_acquire()==ESP_OK); launcher.suspended=false;
    launcher.config=(launcher_config_t){.columns=1,.rows=1,.item_count=1};
    strlcpy(launcher.config.items[0].command,"typemouse",LAUNCHER_COMMAND_MAX);
    solar_os_input_pointer_event_t press={.mode=SOLAR_OS_INPUT_POINTER_ABSOLUTE,
        .action=SOLAR_OS_INPUT_POINTER_PRESS,.buttons=SOLAR_OS_INPUT_POINTER_BUTTON_PRIMARY,.x=100,.y=100};
    assert(launcher_pointer_event(&ctx,&press));
    assert(ctx.requested_app==&solar_os_type_mouse_app &&
           launch_policy==SOLAR_OS_LAUNCH_CHILD_RETURN && launcher.touch_handoff);
    const unsigned before_handoff_releases = releases;
    const solar_os_event_t cancel={.type=SOLAR_OS_EVENT_TOUCH_CANCEL};
    assert(launcher_event(&ctx,&cancel) && launcher.touch_canceling);
    launcher_suspend(&ctx);
    assert(leased && launcher.touch_lease && launcher.touch_handoff &&
           releases==before_handoff_releases);
    launcher_resume(&ctx); /* touch adapter's balanced CANCEL callback */
    assert(leased && !launcher.touch_canceling && launcher.touch_handoff);
    launcher_suspend(&ctx); /* actual child transition must not power-cycle */
    assert(leased && launcher.touch_handoff && releases==before_handoff_releases);
    const unsigned before_resume_acquires = acquires;
    assert(launcher_touch_acquire()==ESP_OK && leased &&
           acquires==before_resume_acquires && releases==before_handoff_releases);
    launcher_stop(&ctx); launcher_touch_release();
    assert(releases==2 && !leased && !ctx.graphics_active);
    puts("launcher_touch_test: 64 grids, custom item, lease failure/lifecycle, TypeMouse child launch PASS");
    /* Load an existing user file, then run the real start/resume callbacks. */
    FILE *f=fopen(config_file,"wb"); assert(f);
    assert(fwrite(launcher_default_config,1,sizeof(launcher_default_config)-1,f)==sizeof(launcher_default_config)-1); fclose(f);
    memset(&state,0,sizeof(state)); ctx=(solar_os_context_t){.gfx=&display};
    assert(launcher_start(&ctx)==ESP_OK && leased && launcher.config.item_count==7);
    launcher.touch_handoff=true;
    launcher_suspend(&ctx); assert(leased);
    launcher_resume(&ctx); assert(leased && !launcher.touch_handoff);
    launcher_suspend(&ctx); assert(!leased);
    launcher_resume(&ctx); assert(leased && !launcher.suspended && launcher.config.item_count==7);
    launcher_stop(&ctx); assert(!leased && writes==0);
    fail_enable=true;
    assert(launcher_start(&ctx)==ESP_FAIL && !leased && !launcher.touch_lease);
    fail_enable=false;
    char contents[sizeof(launcher_default_config)]; size_t length;
    assert(solar_os_storage_read_file(config_file,contents,sizeof(contents),&length)==ESP_OK);
    assert(length==sizeof(launcher_default_config)-1 && !memcmp(contents,launcher_default_config,length));
    assert(remove(config_file)==0);
    puts("launcher_touch_test: actual JSON load/start/resume preserves user file PASS");
    f=fopen(config_file,"wb"); assert(f); fputs("invalid user JSON",f); fclose(f);
    ctx.exit_requested=false;
    assert(launcher_start(&ctx)==ESP_OK && ctx.exit_requested && ctx.exit_code==1 && !leased);
    assert(solar_os_storage_read_file(config_file,contents,sizeof(contents),&length)==ESP_OK);
    assert(length==17 && !memcmp(contents,"invalid user JSON",length) && writes==0);
    assert(remove(config_file)==0);
    puts("launcher_touch_test: failed touch/config start signals session fallback; invalid user file preserved PASS");
    return 0;
}
