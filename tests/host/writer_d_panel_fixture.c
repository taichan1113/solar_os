/* Actual Driver, captured SPI and RAM models, linked to the real pthread
 * executor in another translation unit. Fake panel time has no wall delay. */
#define esp_timer_get_time writer_panel_time
#define vTaskDelay writer_panel_delay
#define SSD1677_TEST_TRANSPORT_HOOK 1
#define SSD1677_DIRTY_TEST_MAIN pipeline_dirty_fixture_main
#include "epd_ssd1677_dirty_hold_test.c"
#undef esp_timer_get_time
#undef vTaskDelay

extern void wd_control_begin(bool);
extern void wd_control_end(esp_err_t);
extern void wd_gate(unsigned);
extern esp_err_t wd_queue_status(solar_os_display_submission_status_t *);
static epd_ssd1677_t device;
static controller_ram_t model;
static bool observing, fail_restore;

static void bridge_begin(void *p, epd_ssd1677_operation_t op)
{ (void)p; wd_control_begin(op==EPD_SSD1677_OP_PRESENT || op==EPD_SSD1677_OP_SLEEP); }
static void bridge_end(void *p, epd_ssd1677_operation_t op, esp_err_t err)
{ (void)p; (void)op; (void)err; wd_control_end(device.faulted ? ESP_FAIL : ESP_OK); }
static esp_err_t bridge_queue(void *p, solar_os_display_submission_status_t *s)
{ (void)p; return wd_queue_status(s); }
esp_err_t ssd1677_test_transport_hook(bool command, uint8_t cmd)
{
    if(!observing || !command) return ESP_OK;
    if(cmd==0x20) wd_gate(0); /* Driver blocked inside its actual activation. */
    if(cmd==0x32) {
        unsigned n=0; for(size_t i=0;i<command_count;i++) if(commands[i].command==0x32) n++;
        if(n) {
            wd_gate(1); /* BUSY/post-sync done; restoration has NOT completed. */
            if(fail_restore) return ESP_FAIL;
        }
    }
    return ESP_OK;
}
void wd_panel_init(uint8_t *out)
{
    device=make_display(); device.papermono_otp=true; device.band_buffer=band;
    device.writer_trial.baseline_selected=true; /* Fixture preparation only. */
    device.fastest_tiles=true; device.fastest_spi_hz=20000000;
    assert(epd_ssd1677_set_controller_mode(&device,"refresh=fastest")==ESP_OK);
    assert(epd_ssd1677_set_controller_mode(&device,"dirty-window=rows-dual")==ESP_OK);
    assert(epd_ssd1677_present_snapshot(&device,frame,sizeof(frame))==ESP_OK);
    flip(20,20);
    assert(epd_ssd1677_present_snapshot(&device,frame,sizeof(frame))==ESP_OK);
    controller_seed(&model,&device); model.swap_after_activation=true;
    device.execution_context=&device; device.operation_begin=bridge_begin; device.operation_end=bridge_end;
    device.submission_status=bridge_queue;
    device.writer_trial.baseline_selected=false; /* Display-wide default D. */
    memcpy(out,frame,sizeof(frame)); observing=true;
}
esp_err_t wd_mode(const char *mode)
{ return epd_ssd1677_writer_lut(&device,mode,NULL,NULL); }
void wd_status(epd_ssd1677_writer_status_t *s)
{ assert(epd_ssd1677_writer_lut(&device,"status",s,NULL)==ESP_OK); }
void wd_fail_restore(void) { fail_restore=true; }
esp_err_t wd_panel_execute(const uint8_t *data,size_t size,const solar_os_writer_frame_t *origin,bool *physical)
{
    command_count=0; memcpy(frame,data,size); /* Only Host model expected-frame input. */
    device.writer_trial.origin=*origin;
    const uint64_t before=device.writer_trial.status.writer_physical+device.writer_trial.status.other_physical;
    const esp_err_t err=epd_ssd1677_present_snapshot(&device,data,size);
    const uint64_t after=device.writer_trial.status.writer_physical+device.writer_trial.status.other_physical;
    *physical=after!=before;
    if(err==ESP_OK && *physical) {
        const transfer_t t=controller_apply(&model,&device);
        assert(t.activations==1 && !memcmp(shadow,data,size));
        assert(!memcmp(model.bw,model.red,sizeof(model.bw)));
        unsigned lut=0; for(size_t i=0;i<command_count;i++) if(commands[i].command==0x32) lut++;
        assert(lut==(device.writer_trial.used_d || device.writer_trial.used_e1?2U:1U));
        const uint8_t *selected=device.writer_trial.used_e1?ssd1677_trial_black_hold_e1_lut:
            device.writer_trial.used_d?ssd1677_trial_black_hold_d_lut:ssd1677_reference_lut;
        assert(!memcmp(find_command(0x32,0)->data,selected,105));
        assert(!memcmp(find_command(0x32,lut-1)->data,ssd1677_reference_lut,105));
    }
    device.writer_trial.origin=(solar_os_writer_frame_t){0};
    return err;
}
