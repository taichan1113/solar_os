#define SSD1677_DIRTY_TEST_MAIN dirty_fixture_main
#include "epd_ssd1677_dirty_hold_test.c"

static void fake_begin(void *p, epd_ssd1677_operation_t op) { (void)p; (void)op; }
static void fake_end(void *p, epd_ssd1677_operation_t op, esp_err_t e) { (void)p; (void)op; (void)e; }
static unsigned count_command(uint8_t cmd)
{
    unsigned n=0; for (size_t i=0;i<command_count;i++) if(commands[i].command==cmd) n++;
    return n;
}
static epd_ssd1677_t prepare(void)
{
    epd_ssd1677_t d=make_display();
    d.papermono_otp=true; d.band_buffer=band; d.fastest_tiles=true; d.fastest_spi_hz=20000000;
    d.writer_trial.baseline_selected=true; /* Fixture preparation, not application policy. */
    assert(epd_ssd1677_set_controller_mode(&d,"refresh=fastest")==ESP_OK);
    assert(epd_ssd1677_set_controller_mode(&d,"dirty-window=rows-dual")==ESP_OK);
    assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK);
    flip(20,20); /* Fixed black pixel well outside later dirty rows. */
    assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK);
    d.execution_context=&d; d.operation_begin=fake_begin; d.operation_end=fake_end;
    d.writer_trial.baseline_selected=false; /* Ordinary global default. */
    command_count=0;
    return d;
}
static void boot(void) /* Host ONLY: simulate a new process/MCU boot. No firmware reset API. */
{ atomic_store(&writer_d_failed,false); stuck_busy=false; }
static void writer(epd_ssd1677_t *d)
{ d->writer_trial.origin=(solar_os_writer_frame_t){.is_writer=true,.inputs=1}; }
static void test_lut(void)
{
    assert(ssd1677_trial_black_hold_d_lut[0]==0x2f && ssd1677_trial_black_hold_d_lut[1]==0xf0);
    for(unsigned i=0;i<110;i++) {
        if(i>=2) assert(ssd1677_trial_black_hold_d_lut[i]==ssd1677_reference_lut[i]);
        if(i<10) assert(ssd1677_reference_lut[i]==0);
    }
    unsigned frames=0;
    for(unsigned group=0;group<10;group++) {
        assert(!ssd1677_reference_lut[54+group*5]); /* no repeat cycles */
        for(unsigned p=0;p<4;p++) frames+=ssd1677_reference_lut[50+group*5+p];
    }
    assert(frames==10);
}
static void test_normal_dirty(bool r2, bool swap)
{
    boot(); epd_ssd1677_t d=prepare();
    u8g2_SetupDisplay(&d.u8g2,ssd1677_u8x8_display_cb,u8x8_dummy_cb,u8x8_dummy_cb,u8x8_dummy_cb);
    u8g2_SetupBuffer(&d.u8g2,frame,60,u8g2_ll_hvline_vertical_top_lsb,r2 ? U8G2_R2 : U8G2_R0);
    u8g2_SetDrawColor(&d.u8g2,2);
    controller_ram_t *model=malloc(sizeof(*model)); assert(model);
    controller_seed(model,&d); model->swap_after_activation=swap;
    epd_ssd1677_writer_status_t status;
    assert(epd_ssd1677_writer_lut(&d,"status",&status,NULL)==ESP_OK && status.enabled);
    assert(epd_ssd1677_writer_lut(&d,"candidate_d",&status,NULL)==ESP_OK && command_count==0);
    writer(&d);
    assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK && !command_count);
    assert(!d.writer_trial.status.attempts); /* no-op cannot count a D update */
    /* A/D share byte-identical three rows-dual payloads and address windows. */
    static captured_command_t saved[128]; size_t saved_count=0;
    for(unsigned candidate=0;candidate<2;candidate++) {
        /* Same old/new image in two independent Driver states. */
        epd_ssd1677_t before=d; uint8_t old[48000]; memcpy(old,shadow,sizeof(old));
        assert(epd_ssd1677_writer_lut(&d,candidate?"candidate_d":"baseline",NULL,NULL)==ESP_OK);
        u8g2_DrawPixel(&d.u8g2,700,321); command_count=0;
        const unsigned px=r2?99:700, py=r2?158:321;
        assert(!(frame[(py/8)*800+px] & (1U<<(py&7))));
        assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK);
        assert(count_command(0x20)==1 && count_command(0x32)==(candidate?2U:1U));
        assert(!memcmp(shadow,frame,sizeof(frame)) && d.dirty_hold_ram_synced);
        const transfer_t tr=controller_apply(model,&d);
        assert(tr.bw_bytes==200 && tr.red_bytes==100);
        if(!candidate) {
            saved_count=command_count; memcpy(saved,commands,sizeof(saved));
            d=before; memcpy(shadow,old,sizeof(old)); u8g2_DrawPixel(&d.u8g2,700,321);
            controller_seed(model,&d); model->swap_after_activation=swap;
        } else {
            assert(command_count==saved_count+1);
            for(size_t i=0;i<saved_count;i++) {
                assert(saved[i].command==commands[i].command && saved[i].length==commands[i].length);
                if(commands[i].command==0x32) {
                    assert(commands[i].data[0]==0x2f && commands[i].data[1]==0xf0);
                    assert(!memcmp(saved[i].data+2,commands[i].data+2,103));
                } else assert(!memcmp(saved[i].data,commands[i].data,commands[i].length));
            }
            assert(commands[command_count-1].command==0x32);
            assert(!memcmp(commands[command_count-1].data,ssd1677_reference_lut,105));
            assert(model->classes[0]>0); /* global RAM model, not proof of optical scope */
        }
    }
    free(model);
    puts("writer D: A/D payload, rows-dual, transition RAM/Shadow, restore, R0/R2 retain/swap PASS");
}
static void test_continuous_and_sources(void)
{
    boot(); epd_ssd1677_t d=prepare();
    epd_ssd1677_writer_status_t status;
    assert(epd_ssd1677_writer_lut(&d,"status",&status,NULL)==ESP_OK && status.enabled);
    const char *apps[]={"Shell","Launcher","Writer","OtherApp","TypeMouse","untagged"};
    /* Every non-Writer uses the ordinary untagged submission. App labels do
     * not enter the Driver at all; origin metadata cannot gate the LUT. */
    for(unsigned i=0;i<21;i++) {
        d.writer_trial.origin=(solar_os_writer_frame_t){.is_writer=(i%6==2),.inputs=1};
        command_count=0; flip(600+i,310);
        assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK);
        assert(count_command(0x20)==1 && count_command(0x32)==2);
        assert(d.writer_trial.status.attempts==i+1 && d.writer_trial.used_d);
        assert(d.writer_trial.status.d_physical==i+1 && d.writer_trial.status.d_success==i+1);
        expect_data(0x32,ssd1677_trial_black_hold_d_lut,105);
        assert(!memcmp(find_command(0x32,1)->data,ssd1677_reference_lut,105));
        assert(epd_ssd1677_writer_lut(&d,"status",&status,NULL)==ESP_OK && status.enabled);
        if(i<6 || i==6 || i==19 || i==20)
            printf("All-app physical update %u (%s): D, single 0x20, restore PASS\n",i+1,apps[i%6]);
    }
    command_count=0;
    assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK && !command_count);
    assert(d.writer_trial.status.attempts==21 && d.writer_trial.status.noop==1);
    assert(epd_ssd1677_writer_lut(&d,"baseline",NULL,NULL)==ESP_OK && !command_count);
    const uint64_t old_a=d.writer_trial.status.a_physical;
    for(unsigned origin=0;origin<2;origin++) {
        d.writer_trial.origin=(solar_os_writer_frame_t){.is_writer=origin};
        flip(699-origin,310); command_count=0;
        assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK);
        assert(count_command(0x20)==1 && count_command(0x32)==1 && !d.writer_trial.used_d);
        expect_data(0x32,ssd1677_reference_lut,105);
    }
    assert(d.writer_trial.status.a_physical==old_a+2 && d.writer_trial.status.attempts==21);
    command_count=0;
    assert(epd_ssd1677_writer_lut(&d,"candidate_d",NULL,NULL)==ESP_OK && !command_count);
    for(unsigned origin=0;origin<2;origin++) {
        d.writer_trial.origin=(solar_os_writer_frame_t){.is_writer=origin};
        flip(690-origin,310); command_count=0;
        assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK);
        assert(count_command(0x20)==1 && count_command(0x32)==2 && d.writer_trial.used_d);
    }
    const uint64_t previous_physical=d.writer_trial.status.writer_physical+d.writer_trial.status.other_physical;
    assert(ssd1677_sleep(&d)==ESP_OK);
    assert(d.writer_trial.status.power_controls==1 &&
           d.writer_trial.status.writer_physical+d.writer_trial.status.other_physical==previous_physical);
    d=prepare();
    assert(epd_ssd1677_writer_lut(&d,"status",&status,NULL)==ESP_OK && status.enabled && !status.attempts);
    puts("all-app D: continuous past 6/7/20, global A/D, no-op, power separation, boot D PASS");
}
static void test_otp_not_compensated(void)
{
    boot(); epd_ssd1677_t d=prepare();
    assert(epd_ssd1677_set_controller_mode(&d,"refresh=full")==ESP_OK);
    flip(480,310); command_count=0;
    assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK);
    assert(!d.writer_trial.used_d && !d.writer_trial.status.attempts);
    assert(count_command(0x32)==0 && d.writer_trial.status.otp_physical>0);
    assert(epd_ssd1677_set_controller_mode(&d,"refresh=fastest")==ESP_OK);
    flip(481,310); command_count=0;
    assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK);
    /* Existing mode transition establishes an OTP baseline before fastest. */
    assert(!d.writer_trial.used_d && !d.writer_trial.status.attempts && !count_command(0x32));
    flip(482,310); command_count=0;
    assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK);
    assert(d.writer_trial.used_d && d.writer_trial.status.d_physical==1 && count_command(0x20)==1);
    puts("all-app D: full/OTP unchanged, next untagged cold fastest D PASS");
}
static void test_sketch_exception(void)
{
    boot(); epd_ssd1677_t d=prepare();
    uint64_t expected_d=0,expected_a=d.writer_trial.status.a_physical;
    for(unsigned i=0;i<5;i++) {
        const bool sketch=(i%2)==1;
        d.writer_trial.origin=(solar_os_writer_frame_t){.is_sketch=sketch};
        flip(470+i,310); command_count=0;
        assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK);
        assert(count_command(0x20)==1 && count_command(0x32)==(sketch?1U:2U));
        expect_data(0x32,sketch?ssd1677_reference_lut:ssd1677_trial_black_hold_d_lut,105);
        if(sketch) expected_a++; else expected_d++;
        assert(d.writer_trial.status.d_physical==expected_d && d.writer_trial.status.a_physical==expected_a);
        assert(!d.writer_trial.baseline_selected && !memcmp(shadow,frame,sizeof(frame)));
    }
    puts("Sketch exception: other D -> Sketch A -> other D, unchanged global mode/Shadow PASS");
}
static void test_synchronous_u8g2(void)
{
    boot(); epd_ssd1677_t d=prepare();
    u8g2_SetupDisplay(&d.u8g2,ssd1677_u8x8_display_cb,u8x8_dummy_cb,u8x8_dummy_cb,u8x8_dummy_cb);
    u8g2_SetupBuffer(&d.u8g2,frame,60,u8g2_ll_hvline_vertical_top_lsb,U8G2_R2);
    u8g2_SetDrawColor(&d.u8g2,2);
    for(unsigned i=0;i<2;i++) {
        u8g2_DrawPixel(&d.u8g2,400+i,250); command_count=0;
        u8g2_SendBuffer(&d.u8g2);
        assert(d.last_error==ESP_OK && count_command(0x20)==1 && count_command(0x32)==2);
        assert(d.writer_trial.status.attempts==i+1 && d.writer_trial.status.d_success==i+1);
        assert(d.writer_trial.status.other_physical>=i+1 && !d.writer_trial.origin.is_writer);
        assert(d.writer_trial.current.restored && !memcmp(shadow,frame,sizeof(frame)));
    }
    command_count=0; u8g2_SendBuffer(&d.u8g2);
    assert(!command_count && d.writer_trial.status.attempts==2 && d.writer_trial.status.noop==1);
    fail_command=0x32; u8g2_DrawPixel(&d.u8g2,403,250); command_count=0;
    u8g2_SendBuffer(&d.u8g2);
    assert(d.last_error!=ESP_OK && d.faulted && atomic_load(&writer_d_failed));
    assert(d.writer_trial.status.d_errors==1 && d.writer_trial.baseline_selected);
    const size_t sent=command_count; u8g2_SendBuffer(&d.u8g2);
    assert(d.last_error==ESP_ERR_INVALID_STATE && command_count==sent);
    puts("all-app D: real synchronous u8g2 callback, no-op, trace, fault stop PASS");
}
static void test_cold_first_fastest(void)
{
    boot(); epd_ssd1677_t d=prepare(); /* No Writer tag, like Shell startup. */
    /* OTP/known Shadow retained, custom controller entry is cold. */
    d.reference_awake=false; d.controller_ready=false;
    flip(500,320); command_count=0;
    assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK);
    assert(d.writer_trial.used_d && d.writer_trial.status.d_physical==1);
    expect_data(0x32,ssd1677_trial_black_hold_d_lut,105);
    expect_data(0x22,(uint8_t[]){0xcc},1);
    assert(count_command(0x20)==1 && count_command(0x32)==2 && d.shadow_valid && d.dirty_hold_ram_synced);
    puts("writer D: first cold fastest activation D; existing cold initialization preserved PASS");
}
static void test_counter_saturation(void)
{
    boot(); epd_ssd1677_t d=prepare(); writer(&d);
    /* Crossing diagnostic totals cannot change policy. No long stress loop. */
    const uint64_t checkpoints[]={99,999,UINT64_MAX-1,UINT64_MAX};
    for(unsigned i=0;i<4;i++) {
        d.writer_trial.status.attempts=checkpoints[i];
        d.writer_trial.status.d_physical=checkpoints[i];
        d.writer_trial.status.d_success=checkpoints[i];
        d.writer_trial.status.writer_physical=checkpoints[i];
        d.writer_trial.status.records=checkpoints[i];
        const unsigned old_next=d.writer_trial.status.trace_next;
        flip(520+i,330); command_count=0;
        assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK && d.writer_trial.used_d);
        const uint64_t expected=checkpoints[i]==UINT64_MAX?UINT64_MAX:checkpoints[i]+1;
        assert(d.writer_trial.status.attempts==expected && d.writer_trial.status.d_physical==expected &&
               d.writer_trial.status.d_success==expected && d.writer_trial.status.writer_physical==expected &&
               d.writer_trial.status.records==expected &&
               d.writer_trial.status.trace_next==(old_next+1)%SSD1677_WRITER_D_RECORDS);
        assert(count_command(0x20)==1 && count_command(0x32)==2);
    }
    assert(solar_os_writer_count_add(UINT32_MAX,1)==UINT32_MAX);
    assert(solar_os_writer_count_add(UINT32_MAX-1,2)==UINT32_MAX);
    puts("writer D: totals 100/1000 and UINT64 saturation never stop D; ring remains bounded PASS");
}
static void test_faults(void)
{
    for(unsigned kind=0;kind<3;kind++) {
        boot(); epd_ssd1677_t d=prepare(); writer(&d);
        assert(epd_ssd1677_writer_lut(&d,"candidate_d",NULL,NULL)==ESP_OK);
        flip(550,300); command_count=0;
        if(kind==0) fail_command=0x32;
        if(kind==1) busy_on_activation=true;
        /* Post-sync failure also prevents unsafe restore/continuation. */
        if(kind==2) { fail_plane_command=0x26; fail_plane_after_bytes=0; }
        assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))!=ESP_OK);
        assert(d.writer_trial.status.attempts==1 && atomic_load(&writer_d_failed));
        assert(d.faulted && !d.shadow_valid && d.writer_trial.baseline_selected);
        const size_t n=command_count;
        assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_ERR_INVALID_STATE && n==command_count);
        assert(epd_ssd1677_writer_lut(&d,"candidate_d",NULL,NULL)==ESP_ERR_INVALID_STATE);
        stuck_busy=false;
    }
    puts("writer D: LUT/BUSY/post-sync failure latches, no retries/refund PASS");
}

static void test_production_f(void)
{
    boot(); epd_ssd1677_t d=prepare();
    d.default_f=true; d.writer_trial.f_selected=true;
    assert(ssd1677_trial_black_hold_f_lut[0]==0 && ssd1677_trial_black_hold_f_lut[1]==0xc0);
    for(unsigned i=0;i<110;++i) if(i!=1) assert(ssd1677_trial_black_hold_f_lut[i]==ssd1677_reference_lut[i]);
    controller_ram_t model; controller_seed(&model,&d); model.swap_after_activation=true;
    for(unsigned i=0;i<35;++i) {
        d.writer_trial.origin=(solar_os_writer_frame_t){.is_writer=i%3==1,.is_sketch=i%3==2};
        flip(300+i,300); command_count=0;
        assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK);
        assert(d.writer_trial.used_f && !d.writer_trial.used_d && d.writer_trial.restored);
        assert(count_command(0x20)==1 && count_command(0x32)==2);
        assert(!memcmp(find_command(0x32,0)->data,ssd1677_trial_black_hold_f_lut,105));
        assert(!memcmp(find_command(0x32,1)->data,ssd1677_reference_lut,105));
        controller_apply(&model,&d);
        assert(!memcmp(model.bw,model.red,sizeof(model.bw)) && !memcmp(shadow,frame,sizeof(frame)));
    }
    assert(d.writer_trial.status.f_physical==35 && d.writer_trial.status.d_physical==0);
    command_count=0; assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK);
    assert(!command_count && d.writer_trial.current.ram_bytes==0); /* no stale timing */
    assert(epd_ssd1677_writer_lut(&d,"candidate_d",NULL,NULL)==ESP_ERR_INVALID_STATE);
    assert(epd_ssd1677_writer_lut(&d,"baseline",NULL,NULL)==ESP_OK);
    flip(400,300);command_count=0;assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK);
    assert(!d.writer_trial.used_f && count_command(0x32)==1);
    assert(epd_ssd1677_writer_lut(&d,"candidate_f",NULL,NULL)==ESP_OK);
    assert(epd_ssd1677_set_power_save(&d,true)==ESP_OK);
    assert(epd_ssd1677_resume(&d)==ESP_OK); /* OTP baseline, F selection retained */
    flip(420,300);command_count=0;assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK);
    assert(d.writer_trial.f_selected); /* Wake first rebuilds the OTP baseline. */
    flip(421,300); command_count=0;
    assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK);
    assert(d.writer_trial.used_f && count_command(0x20)==1);
    puts("production F: all sources, 32-update resync, no-op, A/F recovery and Sleep/Wake PASS");
}

int main(void)
{
    test_production_f();
    test_lut();
    for(unsigned r=0;r<2;r++) for(unsigned swap=0;swap<2;swap++) test_normal_dirty(r,swap);
    test_continuous_and_sources(); test_otp_not_compensated(); test_cold_first_fastest(); test_counter_saturation(); test_faults();
    test_synchronous_u8g2();
    test_sketch_exception();
    puts("writer_d_driver_test: PASS"); return 0;
}
