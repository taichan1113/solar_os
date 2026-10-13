/* Actual Writer + document/GFX/U8g2 + SSD1677 captured SPI. Deterministic
 * asynchronous boundary model; production executor separately tested by
 * display_async_test. No storage/checkpoint call is permitted. */
#define vTaskDelay epd_fixture_delay
#define main existing_epd_main
#include "epd_ssd1677_test.c"
#undef main
#undef vTaskDelay
void vTaskDelay(TickType_t ticks);
#include "freertos/queue.h"
#define SOLAR_OS_BOARD_WRITER_DEFAULT_ZOOM 4
#include "../../src/apps/solar_os_writer.c"
#include "solar_os_gfx_internal.h"
#include <assert.h>

static epd_ssd1677_t display;
static solar_os_gfx_t gfx;
static solar_os_display_submission_status_t status;
static uint8_t pending_frame[48000], active_frame[48000], previous[48000];
static uint64_t pending_id, active_id;
static int64_t finish_at;
static unsigned physical, cleanups, redraws, failure_at;
static bool claimed, continuous_fast;
static const char *gate_keys;
static char output[50000]; static size_t output_len;
static solar_os_log_level_t level=SOLAR_OS_LOG_LEVEL_INFO;
static unsigned fixed_black, fixed_white, first_black;

static long long field(const char *s,const char *name)
{ const char *p=strstr(s,name);assert(p);return strtoll(p+strlen(name),NULL,10); }

static void advance(void)
{
    if (status.busy && time_us>=finish_at) {
        status.busy=false; status.completed_id=active_id; status.completed++; status.physical++;
    }
    if (!status.busy && status.pending && status.error==ESP_OK) {
        memcpy(active_frame,pending_frame,sizeof(active_frame)); active_id=pending_id;
        status.pending=false; status.busy=true; status.started++;
        memcpy(previous,shadow,sizeof(shadow)); command_count=0;
        const bool fast=display.reference_awake || !display.cleanup_pending;
        const int64_t now=time_us;
        /* Keep the boundary busy for warm 112ms (cold >=454ms), even though
         * mocked SPI itself is cheap. BUSY completion still gates promotion. */
        if (fast) time_us+=112000;
        if (status.started==failure_at) busy_on_activation=true;
        esp_err_t err=epd_ssd1677_present_snapshot(&display,active_frame,sizeof(active_frame));
        finish_at=time_us; time_us=now;
        if (err!=ESP_OK) {
            assert(!memcmp(previous,shadow,sizeof(shadow)));
            status.error=err; status.busy=status.pending=false; return;
        }
        assert(!memcmp(shadow,active_frame,sizeof(shadow)) && !display.snapshot);
        if (display.timing.partial) {
            physical++; assert(physical<=10);
            captured_command_t *a=find_command(0x26,0), *b=find_command(0x24,0);
            assert(a->length==48000 && b->length==48000);
            /* Independent scalar address mapping for BOTH real Writer planes. */
            for (unsigned y=0;y<480;y++) for (unsigned x=0;x<800;x++) {
                const unsigned native=(y/8)*800+x, bit=y%8, dest=y*100+x/8, dstbit=7-x%8;
                assert(((a->data[dest]>>dstbit)&1)==((previous[native]>>bit)&1));
                assert(((b->data[dest]>>dstbit)&1)==((active_frame[native]>>bit)&1));
            }
        } else cleanups++;
    }
}
void vTaskDelay(TickType_t t) { epd_fixture_delay(t); advance(); }
esp_err_t solar_os_display_submit(u8g2_t *g,solar_os_display_present_mode_t m,uint64_t *id)
{
    assert(g==&display.u8g2 && m==SOLAR_OS_DISPLAY_PRESENT_GRAPHICS);
    if (status.error) return status.error;
    if (status.pending) status.replaced++;
    memcpy(pending_frame,frame,sizeof(frame)); pending_id=++status.submitted_id;
    status.submitted++; status.pending=true; *id=pending_id;
    return ESP_OK;
}
esp_err_t solar_os_display_wait(u8g2_t *g,uint32_t timeout)
{
    assert(g==&display.u8g2 && timeout==70000);
    if (continuous_fast && writer_state && writer.quality && writer.quality->armed)
        assert(writer.quality->inputs<=1 || writer.quality->inputs==10);
    const uint64_t id=status.submitted_id;
    for (unsigned n=0;status.completed_id<id && !status.error;n++) {
        assert(n<70000); vTaskDelay(1);
    }
    return status.error;
}
esp_err_t solar_os_display_submission_status(u8g2_t *g,solar_os_display_submission_status_t *s)
{ assert(g==&display.u8g2); *s=status; return ESP_OK; }
esp_err_t solar_os_display_get_colors(uint32_t *fg,uint32_t *bg)
{ *fg=0;*bg=0xffffff;return ESP_OK; }
void solar_os_display_present(u8g2_t *g,solar_os_display_present_mode_t m)
{ (void)g;(void)m;assert(!"unexpected synchronous present"); }
esp_err_t solar_os_display_present_surface(u8g2_t *g,const solar_os_display_surface_t *s)
{ (void)g;(void)s;assert(!"unexpected INDEX8 surface");return ESP_FAIL; }
bool solar_os_display_find_target(const char *name,solar_os_display_target_t *t)
{
    *t=(solar_os_display_target_t){.ready=true,.u8g2=&display.u8g2};
    strcpy(t->name,"display0"); strcpy(t->driver,"ssd1677"); return !strcmp(name,"display0");
}
esp_err_t solar_os_display_open_gfx(const char *name,const char *owner,solar_os_gfx_t **g,char *busy,size_t n)
{ assert(!strcmp(name,"display0") && !strcmp(owner,"shell:writer-quality") && n && !claimed); *busy=0; *g=&gfx; claimed=true; return ESP_OK; }
esp_err_t solar_os_display_release(const char *name,const char *owner)
{ (void)name;(void)owner;assert(claimed);claimed=false;return ESP_OK; }
bool solar_os_display_target_is_board_primary(const char *s) { return !strcmp(s,"display0"); }
void solar_os_sessions_mark_foreground_dirty(void)
{ assert(!claimed && display.cleanup_pending && !display.faulted); redraws++; }
esp_err_t solar_os_display_set_controller_mode(const char *s,const char *m)
{ assert(!strcmp(s,"display0"));return epd_ssd1677_set_controller_mode(&display,m); }
solar_os_shell_io_kind_t solar_os_shell_io_kind(const solar_os_shell_io_t *io) { (void)io;return SOLAR_OS_SHELL_IO_KIND_PORT; }
esp_err_t solar_os_shell_io_printf(solar_os_shell_io_t *io,const char *format,...)
{
    (void)io;va_list ap;va_start(ap,format);
    char fragment[192]; /* Same per-call limit as the real Shell. */
    int n=vsnprintf(fragment,sizeof(fragment),format,ap);va_end(ap);
    assert(n>=0 && (size_t)n<sizeof(fragment) && (size_t)n<sizeof(output)-output_len);
    memcpy(output+output_len,fragment,(size_t)n+1);
    size_t line_start=output_len;
    while (line_start && output[line_start-1]!='\n') line_start--;
    const char *s=output+line_start;
    if (strchr(fragment,'\n') && !strncmp(s,"WQ physical ",12) && !failure_at) {
        unsigned ordinal=(unsigned)field(s,"ordinal=");
        assert(field(s,"gen=")>0 && field(s,"fixed_change=")==0);
        assert(field(s,"writes=")==2 && field(s,"activations=")==1);
        if (ordinal==1) {
            fixed_black=(unsigned)field(s,"fixed00=");fixed_white=(unsigned)field(s,"fixed11=");
            first_black=(unsigned)field(s,"first_change=");
            assert(fixed_black && fixed_white && first_black);
        } else {
            assert(field(s,"fixed00=")==fixed_black && field(s,"fixed11=")==fixed_white);
            assert(field(s,"first_change=")==0 && field(s,"first00=")==first_black);
        }
    }
    output_len+=(size_t)n;return ESP_OK;
}
esp_err_t solar_os_shell_io_writeln(solar_os_shell_io_t *io,const char *s)
{ return solar_os_shell_io_printf(io,"%s\n",s); }
esp_err_t solar_os_shell_io_write(solar_os_shell_io_t *io,const char *s)
{ return solar_os_shell_io_printf(io,"%s",s); }
esp_err_t solar_os_shell_io_flush(solar_os_shell_io_t *io) { (void)io;return ESP_OK; }
esp_err_t solar_os_port_read(const solar_os_port_handle_t *port,uint8_t *data,size_t size,uint32_t timeout,size_t *n)
{ (void)port;assert(size==1 && !timeout && *gate_keys);assert(!status.busy && !status.pending);*(char*)data=*gate_keys++;*n=1;return ESP_OK; }
esp_err_t solar_os_log_get_status(solar_os_log_status_t *s) { *s=(solar_os_log_status_t){.level=level};return ESP_OK; }
esp_err_t solar_os_log_set_level(solar_os_log_level_t l) { level=l;return ESP_OK; }
solar_os_gfx_t *solar_os_context_gfx(solar_os_context_t *ctx) { return ctx->gfx; }
void solar_os_context_init(solar_os_context_t *ctx,solar_os_terminal_t *t,solar_os_gfx_t *g)
{ memset(ctx,0,sizeof(*ctx));ctx->terminal=t;ctx->gfx=g; }
void solar_os_context_set_output_handler(solar_os_context_t *c,solar_os_context_output_fn f,void *p) { c->output_fn=f;c->output_user=p; }
void solar_os_context_set_graphics_active(solar_os_context_t *c,bool a) { c->graphics_active=a; }
int solar_os_context_argc(const solar_os_context_t *c) { return c->argc; }
const char *solar_os_context_argv(const solar_os_context_t *c,int i) { return c->argv[i]; }
void solar_os_context_finish(solar_os_context_t *c,int code,const char *message)
{ (void)message;c->exit_requested=true;c->exit_code=code; }
esp_err_t solar_os_app_start(const solar_os_app_t *app,solar_os_context_t *ctx)
{ assert(!*app->state_slot);*app->state_slot=calloc(1,app->state_size);return app->start(ctx); }
void solar_os_app_stop(const solar_os_app_t *app,solar_os_context_t *ctx)
{ app->stop(ctx);free(*app->state_slot);*app->state_slot=NULL; }

/* Deliberately fatal storage boundaries: RAM diagnostics cannot silently write. */
bool solar_os_storage_is_mounted(void) { assert(!"storage called");return false; }
const char *solar_os_storage_mount_point(void) { assert(!"storage called");return ""; }
esp_err_t solar_os_storage_resolve_path(const char *p,char *out,size_t n)
{ (void)p;(void)out;(void)n;assert(!"storage called");return ESP_FAIL; }
esp_err_t solar_os_storage_mkdir(const char *p) { (void)p;assert(!"storage called");return ESP_FAIL; }
esp_err_t solar_os_writer_checkpoint_open(const char *r,const char *d,solar_os_writer_checkpoint_t **c)
{ (void)r;(void)d;(void)c;assert(!"checkpoint opened");return ESP_FAIL; }
void solar_os_writer_checkpoint_close(solar_os_writer_checkpoint_t *c) { assert(!c); }
void solar_os_writer_checkpoint_status(solar_os_writer_checkpoint_t *c,solar_os_writer_checkpoint_status_t *s)
{ (void)c;(void)s;assert(!"checkpoint status"); }
void solar_os_writer_checkpoint_fence(solar_os_writer_checkpoint_t *c) { (void)c;assert(!"checkpoint fence"); }
void solar_os_writer_checkpoint_resume(solar_os_writer_checkpoint_t *c) { (void)c;assert(!"checkpoint resume"); }
esp_err_t solar_os_writer_checkpoint_wait(solar_os_writer_checkpoint_t *c) { (void)c;assert(!"checkpoint wait");return ESP_FAIL; }
esp_err_t solar_os_writer_checkpoint_submit(solar_os_writer_checkpoint_t *c,const solar_os_writer_buffer_t *b,const solar_os_writer_checkpoint_meta_t *m)
{ (void)c;(void)b;(void)m;assert(!"checkpoint submit");return ESP_FAIL; }
esp_err_t solar_os_writer_checkpoint_available(solar_os_writer_checkpoint_t *c,const char *r,const char *d)
{ (void)c;(void)r;(void)d;assert(!"checkpoint available");return ESP_FAIL; }
esp_err_t solar_os_writer_checkpoint_rebind(solar_os_writer_checkpoint_t *c,const char *r,const char *d)
{ (void)c;(void)r;(void)d;assert(!"checkpoint rebind");return ESP_FAIL; }
esp_err_t solar_os_writer_checkpoint_commit(solar_os_writer_checkpoint_t *c,const char *r,const char *d,const solar_os_writer_buffer_t *b,const solar_os_writer_checkpoint_meta_t *m)
{ (void)c;(void)r;(void)d;(void)b;(void)m;assert(!"checkpoint commit");return ESP_FAIL; }
esp_err_t solar_os_writer_checkpoint_read(const char *r,const char *d,char **s,size_t *n,solar_os_writer_checkpoint_meta_t *m,bool *l)
{ (void)r;(void)d;(void)s;(void)n;(void)m;(void)l;assert(!"checkpoint read");return ESP_FAIL; }

static void run(const char *wave,const char *mode,const char *keys,unsigned failure,bool optical)
{
    display=make_display(); display.papermono_otp=true;display.band_buffer=band;
    display.shadow_valid=true;memcpy(shadow,frame,sizeof(shadow));
    display.fastest_tiles=true;display.fastest_spi_hz=40000000;display.refresh_mode=EPD_SSD1677_REFRESH_FASTEST;
    u8g2_SetupDisplay(&display.u8g2,ssd1677_u8x8_display_cb,u8x8_cad_001,ssd1677_u8x8_byte_cb,NULL);
    u8g2_SetupBuffer(&display.u8g2,frame,60,u8g2_ll_hvline_vertical_top_lsb,U8G2_R2);
    solar_os_gfx_init(&gfx,&display.u8g2);
    status=(solar_os_display_submission_status_t){0};
    physical=cleanups=redraws=0;output_len=0;time_us=0;failure_at=failure;gate_keys=keys;claimed=false;stuck_busy=false;
    continuous_fast=optical && !strcmp(mode,"fast");
    char *args[]={"display","test","display0","writer-quality",(char*)wave,(char*)mode,"optical"};
    solar_os_shell_io_t io={0};
    solar_os_writer_quality_command(&io,optical?7:6,args);
    const char *outdir=getenv("WQ_OUTPUT_DIR");
    if (outdir && !failure && keys[0]!='q') {
        char path[512];snprintf(path,sizeof(path),"%s/%s-%s%s.log",outdir,wave,mode,optical?"-optical":"");
        FILE *f=fopen(path,"w");assert(f);fwrite(output,1,output_len,f);fclose(f);
    }
    assert(!writer_state && !claimed && !display.trial_observer);
    if (failure) { assert(display.faulted && !redraws && physical<=10);return; }
    if (redraws!=1 || cleanups<2) fprintf(stderr,"%s\n",output);
    assert(redraws==1 && physical<=10 && cleanups>=2);
    assert(strstr(output,"healthy=1 restored=1"));
    if (keys[0]!='q') {
        if (!strstr(output,"inputs=10 renders=10")) fprintf(stderr,"%s\n",output);
        assert(strstr(output,"inputs=10 renders=10"));
        assert(strstr(output,"fixed_change=0"));
        if (!strcmp(mode,"slow")) assert(physical==10 && status.replaced==0);
        else assert(physical<10 && status.replaced>0);
        if (optical) {
            assert(!strstr(output,"gate=plus3") && !strstr(output,"gate=plus6"));
            assert(strstr(output,"WQ point name=T0") && strstr(output,"WQ point name=T1"));
            assert(strstr(output,"WQ mask which=first") && strstr(output,"changes=unmeasured"));
        }
    }
}
static void test_sparse_observer_matches_full_audit(void)
{
    uint8_t *masks=calloc(2,48000);assert(masks);
    wq_trace_t full={.armed=true,.driver=&display,.fixed={4,37,250,30},
        .first={4,147,10,22},.signature={4,147,100,22},.fixed_mask=masks,.first_mask=masks+48000};
    wq_mask(full.fixed_mask,full.fixed);wq_mask(full.first_mask,full.first);
    wq_trace_t sparse=full;sparse.optical=true;
    for (unsigned i=0;i<48000;i++) {shadow[i]=(uint8_t)(i*31U);frame[i]=(uint8_t)(i*47U);}
    assert(wq_observer(&full,frame,false,ESP_OK)==ESP_OK);
    assert(wq_observer(&sparse,frame,false,ESP_OK)==ESP_OK);
    const wq_physical_t *a=&full.physical[0],*b=&sparse.physical[0];
    assert(a->fixed_changed==b->fixed_changed && a->fixed_00==b->fixed_00 && a->fixed_11==b->fixed_11);
    assert(a->first_changed==b->first_changed && a->first_00==b->first_00);
    assert(b->fixed_changed>0 && b->first_changed>0); /* detects unexpected white/black transitions */
    free(masks);
}
int main(void)
{
    run("baseline","slow","gggggg",0,false);run("dose","slow","gggggg",0,false);
    run("baseline","fast","gggggg",0,false);run("dose","fast","gggggg",0,false);
    run("baseline","slow","gggg",0,true);run("baseline","fast","gggg",0,true);
    wq_trace_t budget={.armed=true,.starts=10,.driver=&display};
    display.trial_observer=wq_observer;display.trial_observer_context=&budget;
    memcpy(previous,shadow,sizeof(previous));command_count=0;
    assert(epd_ssd1677_present_snapshot(&display,frame,sizeof(frame))==ESP_ERR_INVALID_STATE);
    assert(!command_count && !display.snapshot && !memcmp(previous,shadow,sizeof(previous)) && budget.overflow);
    display.trial_observer=NULL;display.trial_observer_context=NULL;
    run("baseline","slow","q",0,false);run("baseline","slow","gg",3,false);
    run("baseline","fast","gg",3,true);
    test_sparse_observer_matches_full_audit();
    puts("writer_quality_test: PASS (real editing/raster, planes, pacing, replacement, cap, abort/fault)");
    return 0;
}
