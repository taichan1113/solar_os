/* Trial-only, serial-modal Writer experiment. Uses the real lifecycle, CHAR,
 * TICK, gap buffer, Markdown layout, GFX raster and immutable async snapshots.
 * No foreground Writer may exist; no checkpoint is opened or saved. */
#include "solar_os_display.h"
#include "solar_os_shell_io.h"
#include "solar_os_sessions.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static esp_err_t writer_quality_output_marker(const char *s, size_t n, void *p)
{ (void)s; (void)n; (void)p; return ESP_OK; }

bool solar_os_writer_quality_active(void)
{
    solar_os_display_target_t t;
    /* Registry lock supplies the cross-task lifetime fence, no mutable global. */
    return solar_os_display_find_target("display0",&t) &&
           !strcmp(t.owner,"shell:writer-quality");
}

static uint32_t wq_hash(const uint8_t *p, size_t n)
{
    uint32_t h = UINT32_C(2166136261);
    for (size_t i=0; i<n; ++i) h = (h ^ p[i]) * UINT32_C(16777619);
    return h;
}

/* Validated R2 U8g2, native 800x480 VLSB. Masks use actual native coordinates,
 * not linear/XBM addressing; hardware tile output is tested independently. */
static void wq_mask(uint8_t *mask, wq_roi_t r)
{
    memset(mask, 0, 48000);
    for (int y=r.y; y<r.y+r.h; ++y) for (int x=r.x; x<r.x+r.w; ++x) {
        if (x<0 || x>=800 || y<0 || y>=480) continue;
        const unsigned nx=799U-(unsigned)x, ny=479U-(unsigned)y;
        mask[(ny/8)*800+nx] |= (uint8_t)(1U << (ny%8));
    }
}

static void writer_quality_render_begin(void)
{
    wq_trace_t *q=writer.quality;
    if (!q->armed) return;
    wq_frame_t *r=&q->frame[q->renders];
    r->chars=q->inputs; r->length=solar_os_writer_buffer_length(&writer.buffer);
    r->doc_start=q->doc_start; r->doc_end=q->doc_end;
    r->render_start=esp_timer_get_time();
}

static void writer_quality_render_end(solar_os_gfx_t *gfx, uint64_t *generation,
                                      esp_err_t *error)
{
    wq_trace_t *q=writer.quality;
    wq_frame_t *r=&q->frame[q->renders];
    r->render_end=esp_timer_get_time();
    int x=0,y=0,h=0,end_x=0,end_y=0;
    if (!solar_os_doc_layout_source_to_xy(&writer.layout,q->initial_length,&x,&y,&h) ||
        !solar_os_doc_layout_source_to_xy(&writer.layout,q->initial_length+1,&end_x,&end_y,NULL) ||
        end_y!=y || end_x<=x || writer.scroll_y!=0) {
        q->overflow=true; *error=ESP_ERR_INVALID_STATE; return;
    }
    const wq_roi_t first={WRITER_MARGIN_X+x,WRITER_HEADER_H+WRITER_MARGIN_Y+y,end_x-x,h};
    if (!q->renders) { q->first=first; wq_mask(q->first_mask,first); }
    else if (memcmp(&first,&q->first,sizeof(first))) {
        q->overflow=true; *error=ESP_ERR_INVALID_STATE; return;
    }
    r->hash=wq_hash(q->driver->buffer,q->driver->buffer_size);
    ++q->renders; /* reservation precedes submission, including active/pending */
    r->submit_start=esp_timer_get_time();
    *error=solar_os_gfx_submit(gfx,generation);
    r->submit_end=esp_timer_get_time(); r->generation=*generation;
    (void)solar_os_gfx_submission_status(gfx,&r->status);
}

static esp_err_t wq_observer(void *context, const uint8_t *frame, bool end, esp_err_t error)
{
    wq_trace_t *q=context;
    if (!q->armed) return ESP_OK;
    if (end) {
        if (!q->starts || q->starts>WQ_LIMIT) return ESP_ERR_INVALID_STATE;
        wq_physical_t *r=&q->physical[q->starts-1];
        r->end=esp_timer_get_time(); r->error=error; r->timing=q->driver->timing;
        if (error==ESP_OK && (r->timing.writes!=2 || r->timing.activations!=1 ||
            q->driver->actual_spi_hz!=40000000 || !q->driver->fastest_tiles)) {
            q->overflow=true; r->error=ESP_ERR_INVALID_STATE;
        }
        q->completed_at=r->end;
        return ESP_OK;
    }
    if (q->starts>=WQ_LIMIT) { q->overflow=true; return ESP_ERR_INVALID_STATE; }
    wq_physical_t *r=&q->physical[q->starts++];
    const int64_t observer_start=esp_timer_get_time();
    const uint8_t *old=q->driver->shadow;
    r->hash=UINT32_C(2166136261); r->x0=800; r->y0=480; r->x1=r->y1=-1;
    for (size_t i=0; i<48000; ++i) {
        const unsigned a=old[i],b=frame[i],diff=a^b;
        const unsigned fm=q->fixed_mask[i], am=q->first_mask[i];
        r->hash=(r->hash ^ b)*UINT32_C(16777619);
        if (fm) {
            r->fixed_changed+=(unsigned)__builtin_popcount(diff&fm);
            r->fixed_00+=(unsigned)__builtin_popcount(~(a|b)&fm);
            r->fixed_11+=(unsigned)__builtin_popcount(a&b&fm);
        }
        if (am) {
            r->first_changed+=(unsigned)__builtin_popcount(diff&am);
            r->first_00+=(unsigned)__builtin_popcount(~(a|b)&am);
        }
        if (diff) {
            r->changed+=(unsigned)__builtin_popcount(diff);
            const int x=(int)(i%800);
            if (x<r->x0) r->x0=x;
            if (x>r->x1) r->x1=x;
            for (unsigned bit=0;bit<8;bit++) if (diff&(1U<<bit)) {
                const int y=(int)(i/800)*8+(int)bit;
                if (y<r->y0) r->y0=y;
                if (y>r->y1) r->y1=y;
            }
        }
    }
    r->observer_us=esp_timer_get_time()-observer_start;
    r->start=esp_timer_get_time(); /* driver call follows; overhead reported */
    return ESP_OK;
}

static bool wq_gate(solar_os_shell_io_t *io, wq_trace_t *q, const char *stage)
{
    solar_os_shell_io_printf(io,"WQ gate=%s inputs=%u renders=%u starts=%u g=next q=abort\n",
                            stage,q->inputs,q->renders,q->starts);
    solar_os_shell_io_flush(io);
    for (;;) {
        uint8_t key=0; size_t n=0;
        esp_err_t err=solar_os_port_read(&io->port,&key,1,0,&n);
        if (err!=ESP_OK && err!=ESP_ERR_TIMEOUT) return false;
        if (n && (key=='g' || key=='q' || key==27 || key==3 || key==0x1d)) {
            solar_os_shell_io_printf(io,"WQ observation stage=%s ack_us=%lld since_complete_us=%lld key=%u\n",
                stage,(long long)esp_timer_get_time(),
                (long long)(q->completed_at ? esp_timer_get_time()-q->completed_at : 0),key);
            return key=='g';
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static esp_err_t wq_drain(wq_trace_t *q)
{
    esp_err_t err=solar_os_display_wait(&q->driver->u8g2,70000);
    solar_os_display_submission_status_t s={0};
    if (err==ESP_OK) err=solar_os_display_submission_status(&q->driver->u8g2,&s);
    if (err==ESP_OK && (s.busy || s.pending || s.error || q->driver->faulted ||
        !q->driver->shadow_valid || q->driver->snapshot || q->overflow)) err=ESP_ERR_INVALID_STATE;
    return err;
}

/* Use the app's declared 40ms Tick cadence. In the Host three-character burst,
 * 80ms can leave no pending replacement (40,80,160ms raster deadlines).
 * 60ms gives 40,80,120ms deadlines against measured warm ~112ms. Actual
 * device input times and replacement counts still decide the interpretation. */
static esp_err_t wq_segment(solar_os_context_t *ctx, wq_trace_t *q, unsigned until, bool slow)
{
    int64_t next_input=esp_timer_get_time(), next_tick=next_input+40000;
    while (q->inputs<until || writer.render_pending) {
        const int64_t now=esp_timer_get_time();
        if (q->inputs<until && now>=next_input && !writer.render_pending) {
            wq_input_t *in=&q->input[q->inputs]; in->sent=now;
            const solar_os_event_t e={.type=SOLAR_OS_EVENT_CHAR,.data.ch="abcdefghij"[q->inputs]};
            if (!writer_event(ctx,&e)) return ESP_ERR_INVALID_STATE;
            next_input=now+WQ_FAST_INTERVAL_US;
        }
        if (now>=next_tick) {
            const solar_os_event_t e={.type=SOLAR_OS_EVENT_TICK,.data.tick_ms=(uint32_t)(now/1000)};
            writer_event(ctx,&e);
            next_tick=now+40000;
            if (writer.display_error || q->overflow) return ESP_ERR_INVALID_STATE;
            if (slow && !writer.render_pending) {
                esp_err_t err=wq_drain(q);
                if (err!=ESP_OK) return err;
                next_input=esp_timer_get_time();
                next_tick=next_input+40000;
            }
        }
        solar_os_display_submission_status_t s={0};
        if (solar_os_display_submission_status(&q->driver->u8g2,&s)!=ESP_OK || s.error)
            return ESP_ERR_INVALID_STATE;
        vTaskDelay(1);
    }
    return wq_drain(q); /* optical stop sees completed content, never pending */
}

static void wq_dump(solar_os_shell_io_t *io, wq_trace_t *q,
                    const solar_os_display_submission_status_t *before)
{
    solar_os_display_submission_status_t after={0};
    (void)solar_os_display_submission_status(&q->driver->u8g2,&after);
    solar_os_shell_io_printf(io,"WQ summary inputs=%u renders=%u submitted=%u replaced=%u started=%u completed=%u physical=%u overflow=%u\n",
        q->inputs,q->renders,after.submitted-before->submitted,after.replaced-before->replaced,
        after.started-before->started,after.completed-before->completed,after.physical-before->physical,q->overflow);
    solar_os_shell_io_printf(io,"WQ roi fixed=%d,%d,%d,%d first=%d,%d,%d,%d rotation=R2\n",
        q->fixed.x,q->fixed.y,q->fixed.w,q->fixed.h,q->first.x,q->first.y,q->first.w,q->first.h);
    for (unsigned i=0;i<q->inputs;i++) {
        const wq_input_t *r=&q->input[i];
        solar_os_shell_io_printf(io,"WQ input seq=%u ch=%u len=%u sent=%lld accepted=%lld mutated=%lld request=%lld\n",
            r->seq,r->ch,r->length,(long long)r->sent,(long long)r->accepted,(long long)r->mutated,(long long)r->request);
    }
    for (unsigned i=0;i<q->renders;i++) {
        const wq_frame_t *r=&q->frame[i];
        /* Shell printf has a 192-byte buffer. Assemble each journal row from
         * bounded fragments; emit the newline only after its final field. */
        solar_os_shell_io_printf(io,"WQ frame chars=%u len=%u gen=%llu hash=%08lx ",
            r->chars,r->length,(unsigned long long)r->generation,(unsigned long)r->hash);
        solar_os_shell_io_printf(io,"doc_start=%lld doc_end=%lld render_start=%lld render_end=%lld ",
            (long long)r->doc_start,(long long)r->doc_end,(long long)r->render_start,(long long)r->render_end);
        solar_os_shell_io_printf(io,"submit_start=%lld submit_end=%lld replaced=%u completed=%llu busy=%u pending=%u cursor=0\n",
            (long long)r->submit_start,(long long)r->submit_end,r->status.replaced,
            (unsigned long long)r->status.completed_id,r->status.busy,r->status.pending);
    }
    for (unsigned i=0;i<q->starts;i++) {
        const wq_physical_t *r=&q->physical[i]; const epd_ssd1677_timing_t *t=&r->timing;
        unsigned chars=0; uint64_t gen=0;
        for (unsigned j=0;j<q->renders;j++) if (q->frame[j].hash==r->hash) {
            chars=q->frame[j].chars; gen=q->frame[j].generation;
        }
        solar_os_shell_io_printf(io,"WQ physical ordinal=%u gen=%llu chars=%u hash=%08lx start=%lld end=%lld err=%d ",
            i+1,(unsigned long long)gen,chars,(unsigned long)r->hash,(long long)r->start,(long long)r->end,r->error);
        solar_os_shell_io_printf(io,"changes=%u bounds=%d,%d,%d,%d fixed_change=%u fixed00=%u fixed11=%u ",
            r->changed,r->x0,r->y0,r->x1,r->y1,r->fixed_changed,r->fixed_00,r->fixed_11);
        solar_os_shell_io_printf(io,"first_change=%u first00=%u first_following=%u observer_us=%lld total_us=%lld ",
            r->first_changed,r->first_00,i,(long long)r->observer_us,(long long)(t->end_us-t->start_us));
        solar_os_shell_io_printf(io,"busy_us=%lld convert_us=%lld spi_us=%lld writes=%u activations=%u spi_hz=%d path=tile\n",
            (long long)(t->ready_us[0]-t->activate_us[0]),(long long)(t->convert_us[0]+t->convert_us[1]),
            (long long)(t->spi_us[0]+t->spi_us[1]),t->writes,t->activations,q->driver->actual_spi_hz);
    }
}

void solar_os_writer_quality_command(solar_os_shell_io_t *io, int argc, char **argv)
{
    static const char owner[]="shell:writer-quality";
    if (argc!=6 || strcmp(argv[2],"display0") || strcmp(argv[3],"writer-quality") ||
        (strcmp(argv[4],"baseline") && strcmp(argv[4],"dose")) ||
        (strcmp(argv[5],"slow") && strcmp(argv[5],"fast")) ||
        solar_os_shell_io_kind(io)!=SOLAR_OS_SHELL_IO_KIND_PORT) {
        solar_os_shell_io_writeln(io,"usage (serial): display test display0 writer-quality baseline|dose slow|fast"); return;
    }
    solar_os_display_target_t target;
    if (writer_state || !solar_os_display_find_target(argv[2],&target) || !target.ready ||
        !target.u8g2 || strcmp(target.driver,"ssd1677")) {
        solar_os_shell_io_writeln(io,"WQ rejected: close Writer; ready SSD1677 required"); return;
    }
    epd_ssd1677_t *d=(epd_ssd1677_t *)((uint8_t *)target.u8g2 - offsetof(epd_ssd1677_t,u8g2));
    if (!d->papermono_otp || d->refresh_mode!=EPD_SSD1677_REFRESH_FASTEST ||
        !d->fastest_tiles || d->fastest_spi_hz!=40000000 ||
        d->buffer_size!=48000 || d->buffer!=u8g2_GetBufferPtr(target.u8g2) ||
        target.black_is_one || target.u8g2->cb!=U8G2_R2 ||
        u8g2_GetDisplayWidth(target.u8g2)!=800 || u8g2_GetDisplayHeight(target.u8g2)!=480 ||
        d->trial_observer || !d->shadow || !d->band_buffer) {
        solar_os_shell_io_writeln(io,"WQ rejected: PaperMono R2/800x480/40MHz/tile required"); return;
    }
    solar_os_gfx_t *gfx=NULL; char busy[SOLAR_OS_DISPLAY_TARGET_OWNER_MAX]={0};
    esp_err_t err=solar_os_display_open_gfx(target.name,owner,&gfx,busy,sizeof(busy));
    if (err!=ESP_OK) { solar_os_shell_io_printf(io,"WQ claim err=%d owner=%s\n",err,busy); return; }
    wq_trace_t *q=solar_os_memory_calloc(1,sizeof(*q),SOLAR_OS_MEMORY_EXTERNAL_REQUIRED,"writer-quality");
    solar_os_context_t *ctx=solar_os_memory_calloc(1,sizeof(*ctx),SOLAR_OS_MEMORY_EXTERNAL_REQUIRED,"writer-quality-context");
    const epd_ssd1677_refresh_mode_t original_refresh=d->refresh_mode;
    const epd_ssd1677_waveform_trial_t original_waveform=d->waveform_trial;
    const bool original_timing=d->timing_enabled;
    solar_os_log_status_t log={0}; (void)solar_os_log_get_status(&log);
    solar_os_display_submission_status_t before={0};
    bool healthy=false,started=false,configured=false;
    if (!q || !ctx) { err=ESP_ERR_NO_MEM; goto done; }
    q->driver=d;
    q->fixed_mask=solar_os_memory_calloc(1,96000,SOLAR_OS_MEMORY_EXTERNAL_REQUIRED,"writer-quality-masks");
    if (!q->fixed_mask) { err=ESP_ERR_NO_MEM; goto done; }
    q->first_mask=q->fixed_mask+48000;
    err=wq_drain(q); if (err!=ESP_OK) goto done;
    healthy=true; configured=true;
    (void)solar_os_log_set_level(SOLAR_OS_LOG_LEVEL_INFO); /* no per-input UART logs */
    err=solar_os_display_set_controller_mode(target.name,"refresh=fastest");
    if (err==ESP_OK) err=solar_os_display_set_controller_mode(target.name,"timing=off");
    if (err==ESP_OK) err=solar_os_display_set_controller_mode(target.name,"refresh=cleanup");
    if (err!=ESP_OK) goto done;
    solar_os_gfx_clear(gfx,SOLAR_OS_GFX_COLOR_WHITE);
    uint64_t gen=0;
    err=solar_os_gfx_submit(gfx,&gen); if (err==ESP_OK) err=wq_drain(q);
    if (err!=ESP_OK) { healthy=false; goto done; }
    q->completed_at=esp_timer_get_time();
    solar_os_shell_io_printf(io,"WQ begin waveform=%s mode=%s text=abcdefghij zoom=4 spi=40000000 path=tile tick_ms=40 fast_interval_ms=60 max_fast=10 storage=none\n",argv[4],argv[5]);
    if (!wq_gate(io,q,"white")) goto done;
    solar_os_context_init(ctx,NULL,gfx);
    solar_os_context_set_output_handler(ctx,writer_quality_output_marker,q);
    err=solar_os_display_set_controller_mode(target.name,"refresh=cleanup");
    if (err==ESP_OK) {
        err=solar_os_app_start(&solar_os_writer_app,ctx);
        started=writer_state!=NULL && writer.quality==q;
    }
    if (err==ESP_OK) err=wq_drain(q);
    if (err!=ESP_OK) { healthy=false; goto done; }
    q->fixed=(wq_roi_t){WRITER_MARGIN_X,WRITER_HEADER_H+WRITER_MARGIN_Y,250,30};
    wq_mask(q->fixed_mask,q->fixed);
    q->completed_at=esp_timer_get_time();
    if (!wq_gate(io,q,"reference")) goto done;
    err=solar_os_display_set_controller_mode(target.name,
          !strcmp(argv[4],"dose") ? "fastest-waveform=dose" : "fastest-waveform=baseline");
    if (err!=ESP_OK) goto done;
    (void)solar_os_display_submission_status(target.u8g2,&before);
    d->trial_observer_context=q; d->trial_observer=wq_observer; q->armed=true;
    const unsigned stops[]={1,4,7,10}; const char *stages[]={"first","plus3","plus6","final"};
    for (unsigned i=0;i<4;i++) {
        err=wq_segment(ctx,q,stops[i],!strcmp(argv[5],"slow") || i==0);
        if (err!=ESP_OK) { healthy=false; break; }
        if (!wq_gate(io,q,stages[i])) break;
    }
    if (q->armed) wq_dump(io,q,&before);
done:
    /* Faults latch the executor: do not retry cleanup or invalidate shadow. */
    if (configured && q && q->driver) {
        if (healthy && wq_drain(q)!=ESP_OK) healthy=false;
        d->trial_observer=NULL; d->trial_observer_context=NULL; q->armed=false;
        if (healthy) {
            esp_err_t clean=solar_os_display_set_controller_mode(target.name,"refresh=cleanup");
            uint64_t gen=0;
            if (clean==ESP_OK) clean=solar_os_gfx_submit(gfx,&gen);
            if (clean==ESP_OK) clean=wq_drain(q);
            if (clean!=ESP_OK) { err=clean; healthy=false; }
        }
        d->waveform_trial=original_waveform;
        d->refresh_mode=original_refresh; d->timing_enabled=original_timing;
        (void)solar_os_log_set_level(log.level);
    }
    if (started) solar_os_app_stop(&solar_os_writer_app,ctx);
    if (q) solar_os_memory_free(q->fixed_mask);
    solar_os_memory_free(q); solar_os_memory_free(ctx);
    (void)solar_os_display_release(target.name,owner);
    if (configured && healthy && solar_os_display_target_is_board_primary(target.name)) {
        (void)solar_os_display_set_controller_mode(target.name,"refresh=cleanup");
        solar_os_sessions_mark_foreground_dirty(); /* full Terminal cache invalidation */
    }
    solar_os_shell_io_printf(io,"WQ exit err=%d healthy=%u restored=%u\n",err,healthy,configured&&healthy);
}
