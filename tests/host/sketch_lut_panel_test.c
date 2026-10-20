/* Real Sketch event/render/lifecycle callbacks with a completion-fence model. */
#define main sketch_existing_regression_main
#include "sketch_lifecycle_test.c"
#undef main
#undef sketch
#define panel_sketch (*(sketch_state_t *)sketch_state_storage)

static epd_ssd1677_t panel_driver;
static epd_ssd1677_writer_status_t panel_status;
static unsigned drained_submits, controls, starts, stops, cleanup_requests;
static bool cleanup_pending, omit_cleanup, fail_wait, fail_mode, target_missing;
static unsigned actual_mode;

static esp_err_t panel_wait(void *context, uint32_t timeout)
{
    assert(context == &panel_driver && timeout == 10000);
    if (fail_wait) return ESP_ERR_TIMEOUT;
    if (submits != drained_submits) {
        if (cleanup_pending) {
            if (!omit_cleanup) panel_status.otp_physical += 2;
            panel_status.records++;
            cleanup_pending = false;
        } else if (panel_status.trial.active) {
            panel_status.trial.physical += submits - drained_submits;
            panel_status.trial.sketch += submits - drained_submits;
            if (actual_mode == 0) panel_status.trial.a += submits - drained_submits;
            if (actual_mode == 1) panel_status.trial.d += submits - drained_submits;
            if (actual_mode == 2) panel_status.trial.e1 += submits - drained_submits;
        }
        drained_submits = submits;
    }
    return ESP_OK;
}
bool solar_os_display_find_target(const char *name, solar_os_display_target_t *target)
{
    assert(!strcmp(name,"display0"));
    if (target_missing) return false;
    memset(target,0,sizeof(*target));
    target->ready=true;
    strcpy(target->driver,"ssd1677");
    target->u8g2=&panel_driver.u8g2;
    target->submission=(solar_os_display_submission_t){.context=&panel_driver,.wait=panel_wait};
    return true;
}
esp_err_t solar_os_display_set_controller_mode(const char *name,const char *mode)
{
    assert(!strcmp(name,"display0"));
    if (!strcmp(mode,"refresh=cleanup")) {cleanup_pending=true;cleanup_requests++;}
    else assert(!strcmp(mode,"refresh=fastest") || !strcmp(mode,"fastest-path=tile") ||
                !strcmp(mode,"dirty-window=rows-dual") || !strcmp(mode,"spi-mhz=20"));
    return ESP_OK;
}
esp_err_t epd_ssd1677_writer_lut(epd_ssd1677_t *driver,const char *mode,
    epd_ssd1677_writer_status_t *status,epd_ssd1677_writer_record_t *records)
{
    assert(driver==&panel_driver && !records);
    controls++;
    esp_err_t err=ESP_OK;
    if (!strcmp(mode,"baseline")) actual_mode=0;
    else if (!strcmp(mode,"candidate_d")) actual_mode=0; /* Sketch exception */
    else if (!strcmp(mode,"candidate_d_sketch") || !strcmp(mode,"candidate_e1")) {
        if (fail_mode) err=ESP_ERR_INVALID_STATE;
        else actual_mode=!strcmp(mode,"candidate_e1")?2:1;
    } else if (!strcmp(mode,"trial-start")) {
        assert(!panel_status.trial.active && !cleanup_pending && drained_submits==submits);
        panel_status.trial=(epd_ssd1677_trial_summary_t){.started=true,.active=true};
        starts++;
    } else if (!strcmp(mode,"trial-stop")) {
        assert(panel_status.trial.active);
        panel_status.trial.active=false;
        stops++;
    } else assert(!strcmp(mode,"status") || !strcmp(mode,"trial-status"));
    if(status)*status=panel_status;
    return err;
}
static void tap(solar_os_context_t *ctx,int x,int y)
{
    pointer(ctx,SOLAR_OS_INPUT_POINTER_PRESS,x,y);
    pointer(ctx,SOLAR_OS_INPUT_POINTER_RELEASE,x,y);
    tick(ctx);
}
static void row_tap(solar_os_context_t *ctx,unsigned row,int column)
{
    const sketch_layout_t l=sketch_layout(ctx->gfx);
    const int width=(l.screen_width-48)/3;
    tap(ctx,column<0?l.screen_width/2:20+column*(width+4)+width/2,
        sketch_lut_button_y(&l,row)+20);
}
static void open_panel(solar_os_context_t *ctx)
{
    tap(ctx,(int)display.width-50,12);
    assert(panel_sketch.modal==SKETCH_MODAL_LUT);
}
static void select_and_confirm(solar_os_context_t *ctx,unsigned mode)
{
    row_tap(ctx,0,(int)mode);
    row_tap(ctx,1,-1);
    assert(panel_sketch.modal==SKETCH_MODAL_LUT_CONFIRM);
    row_tap(ctx,1,-1);
}
static void reset_panel(void)
{
    memset(&panel_status,0,sizeof(panel_status));
    drained_submits=controls=starts=stops=cleanup_requests=actual_mode=0;
    cleanup_pending=omit_cleanup=fail_wait=fail_mode=target_missing=false;
}
int main(void)
{
    /* Existing drawing, PNG, touch, allocation and lifecycle regression also
     * runs with the panel compiled in, rather than a separate mock app. */
    assert(sketch_existing_regression_main()==0);
    for (unsigned portrait=0;portrait<2;portrait++) {
        display.width=portrait?480:800; display.height=portrait?800:480;
        for (unsigned mode=0;mode<3;mode++) {
            reset_panel();
            solar_os_context_t ctx=begin(0);
            assert(sketch_start(&ctx)==ESP_OK);
            panel_sketch.canvas.pixels[0]=3; panel_sketch.dirty=true;
            strcpy(panel_sketch.path,"/saved-original.png");
            open_panel(&ctx);
            row_tap(&ctx,0,(int)mode); row_tap(&ctx,1,-1);
            assert(panel_sketch.canvas.pixels[0]==3 && !strcmp(panel_sketch.path,"/saved-original.png"));
            row_tap(&ctx,2,-1); /* Cancel never clears unsaved work. */
            assert(panel_sketch.modal==SKETCH_MODAL_LUT && panel_sketch.canvas.pixels[0]==3);
            select_and_confirm(&ctx,mode);
            assert(panel_sketch.lut_running && panel_status.trial.active && actual_mode==mode);
            assert(starts==1 && cleanup_requests==1 && panel_status.otp_physical==2);
            assert(!panel_status.trial.physical && !panel_sketch.path[0] && !panel_sketch.drawing);
            const sketch_layout_t l=sketch_layout(ctx.gfx);
            pointer(&ctx,SOLAR_OS_INPUT_POINTER_PRESS,l.canvas_x+20,l.canvas_y+20);
            pointer(&ctx,SOLAR_OS_INPUT_POINTER_RELEASE,l.canvas_x+25,l.canvas_y+21);
            tick(&ctx);
            assert(panel_sketch.lut_running && panel_status.trial.physical>0);
            open_panel(&ctx);
            assert(!panel_sketch.lut_running && !panel_status.trial.active && actual_mode==0);
            const uint64_t frozen=panel_sketch.lut_result.trial.physical;
            row_tap(&ctx,0,2); tick(&ctx);
            assert(panel_sketch.lut_result.trial.physical==frozen && stops==1);
            row_tap(&ctx,2,-1); /* Normal/Back preserves canvas and fixed summary. */
            assert(panel_sketch.modal==SKETCH_MODAL_NONE && panel_sketch.dirty);
            end(&ctx);
        }
    }
    display.width=800;display.height=480;
    /* Automatic stop at a stroke boundary; the UI frame is outside stats. */
    reset_panel();solar_os_context_t ctx=begin(0);assert(sketch_start(&ctx)==ESP_OK);
    open_panel(&ctx);select_and_confirm(&ctx,1);
    panel_status.trial.physical=20;panel_status.trial.d=20;
    panel_sketch.lut_check_pending=true;tick(&ctx);
    assert(!panel_sketch.lut_running && panel_sketch.modal==SKETCH_MODAL_LUT && actual_mode==0);
    assert(panel_sketch.lut_result.trial.physical==20);end(&ctx);
    /* Suspend stops the interval and restores normal policy without saving. */
    reset_panel();ctx=begin(0);assert(sketch_start(&ctx)==ESP_OK);
    open_panel(&ctx);select_and_confirm(&ctx,2);sketch_suspend(&ctx);
    assert(stops==1 && !panel_sketch.lut_running && actual_mode==0);end(&ctx);
    for (unsigned failure=0;failure<3;failure++) {
        reset_panel();ctx=begin(0);assert(sketch_start(&ctx)==ESP_OK);open_panel(&ctx);
        omit_cleanup=failure==0;fail_mode=failure==1;fail_wait=failure==2;
        select_and_confirm(&ctx,1);
        assert(!starts && !panel_sketch.lut_running && panel_sketch.lut_error!=ESP_OK);
        assert(panel_sketch.modal==SKETCH_MODAL_LUT);
        fail_wait=false;end(&ctx);
    }
    reset_panel();ctx=begin(0);assert(sketch_start(&ctx)==ESP_OK);
    target_missing=true;open_panel(&ctx);
    assert(panel_sketch.lut_error!=ESP_OK && !starts);target_missing=false;end(&ctx);
    puts("standalone Sketch LUT panel: PASS");
    return 0;
}
