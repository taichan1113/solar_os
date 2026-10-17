/* Real Driver/SPI and existing retain/swap Controller RAM models. */
#define SSD1677_DIRTY_TEST_MAIN dirty_existing_main
#include "epd_ssd1677_dirty_hold_test.c"

static void fresh_boot(void)
{
    memset(black_hold_candidate_spent,0,sizeof(black_hold_candidate_spent));
    black_hold_failed=false; /* Host only, never done by display reset. */
}
static epd_ssd1677_t prepared(bool r2)
{
    epd_ssd1677_t d=make_display();
    d.papermono_otp=true; d.fastest_tiles=true; d.band_buffer=band;
    d.fastest_spi_hz=d.configured_spi_hz=20000000;
    d.dirty_hold_trial=d.dirty_hold_full_width=d.dirty_hold_dual_sync=true;
    d.u8g2.cb=r2?U8G2_R2:U8G2_R0;
    frame[0]=0x7f; frame[900]=0x55; frame[47999]=0x81;
    assert(epd_ssd1677_set_controller_mode(&d,"refresh=fastest")==ESP_OK);
    assert(ssd1677_refresh(&d)==ESP_OK);
    assert(epd_ssd1677_black_hold_arm(&d,EPD_SSD1677_BLACK_HOLD_BASELINE)==ESP_OK);
    command_count=0;
    assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK);
    assert(d.reference_awake && d.dirty_hold_ram_synced);
    expect_data(0x22,(uint8_t[]){0xcc},1);
    return d;
}
static void test_identical_bd(bool r2,bool swap,bool reverse)
{
    fresh_boot(); epd_ssd1677_t d=prepared(r2);
    controller_ram_t *m=malloc(sizeof(*m)); assert(m);
    controller_seed(m,&d); m->swap_after_activation=swap;
    command_count=0;
    assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK && !command_count);
    captured_command_t *first=malloc(sizeof(commands)); assert(first); size_t first_count=0;
    for(unsigned i=0;i<2;++i) {
        epd_ssd1677_black_hold_t c=(i==(unsigned)reverse)?EPD_SSD1677_BLACK_HOLD_B:EPD_SSD1677_BLACK_HOLD_D;
        epd_ssd1677_black_hold_t other=c==EPD_SSD1677_BLACK_HOLD_B?EPD_SSD1677_BLACK_HOLD_D:EPD_SSD1677_BLACK_HOLD_B;
        command_count=0;
        assert(epd_ssd1677_black_hold_arm(&d,c)==ESP_OK);
        assert(epd_ssd1677_black_hold_arm(&d,other)==ESP_ERR_INVALID_STATE);
        assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK);
        assert(controller_apply(m,&d).activations==1);
        expect_data(0x22,(uint8_t[]){0x0c},1);
        expect_data(0x03,(uint8_t[]){0x17},1);
        expect_data(0x04,(uint8_t[]){0x46,0xa8,0x32},3);
        expect_data(0x2c,(uint8_t[]){0x30},1);
        assert(find_command(0x32,0)->data[0]==(c==EPD_SSD1677_BLACK_HOLD_B?0x8f:0x2f));
        assert(!memcmp(find_command(0x32,0)->data+1,ssd1677_trial_black_hold_d_lut+1,104));
        assert(!memcmp(find_command(0x32,1)->data,ssd1677_reference_lut,105));
        unsigned luts=0,activations=0;
        if(i) assert(first_count==command_count);
        for(size_t k=0;k<command_count;++k) {
            if(i) {
                assert(first[k].command==commands[k].command && first[k].length==commands[k].length);
                if(commands[k].command==0x32 && !luts) {
                    assert(first[k].data[0]!=commands[k].data[0]);
                    assert(!memcmp(first[k].data+1,commands[k].data+1,104));
                } else assert(!memcmp(first[k].data,commands[k].data,first[k].length));
            }
            if(commands[k].command==0x32) ++luts;
            if(commands[k].command==0x20) ++activations;
        }
        assert(luts==2 && activations==1);
        if(!i) { first_count=command_count; memcpy(first,commands,sizeof(commands)); }
        assert(d.black_hold_lut_ready_us<=d.timing.activate_us[0]);
        assert(d.timing.activate_us[0]<=d.timing.ready_us[0]);
        assert(d.timing.ready_us[0]<=d.black_hold_restore_start_us);
        assert(d.black_hold_restore_start_us<=d.black_hold_restore_end_us);
        assert(d.black_hold_restore_end_us<=d.timing.end_us);
        assert(m->classes[0]>0 && m->classes[1]==0 && m->classes[2]==0 && m->classes[3]>0);
        assert(!memcmp(m->bw,m->red,sizeof(m->bw)) && !memcmp(d.shadow,frame,sizeof(frame)));
        assert(!d.black_hold_active && !d.black_hold_pending && epd_ssd1677_black_hold_spent(c));
        assert(epd_ssd1677_black_hold_arm(&d,c)==ESP_ERR_INVALID_STATE);
    }
    command_count=0;
    assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK && !command_count);
    frame[15]^=1;
    assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK);
    expect_data(0x32,ssd1677_reference_lut,105);
    d=make_display();
    assert(epd_ssd1677_black_hold_spent(EPD_SSD1677_BLACK_HOLD_B) && epd_ssd1677_black_hold_spent(EPD_SSD1677_BLACK_HOLD_D));
    free(first); free(m);
}
static void test_failure(epd_ssd1677_black_hold_t c,unsigned fault)
{
    fresh_boot(); epd_ssd1677_t d=prepared(false);
    assert(epd_ssd1677_black_hold_arm(&d,(epd_ssd1677_black_hold_t)3)==ESP_ERR_INVALID_ARG);
    assert(epd_ssd1677_black_hold_arm(&d,(epd_ssd1677_black_hold_t)-1)==ESP_ERR_INVALID_ARG);
    d.reference_awake=false;
    assert(epd_ssd1677_black_hold_arm(&d,c)==ESP_ERR_INVALID_STATE && !epd_ssd1677_black_hold_spent(c));
    d.reference_awake=true;
    assert(epd_ssd1677_black_hold_arm(&d,c)==ESP_OK); command_count=0;
    if(fault==0) fail_command=0x32;
    if(fault==1) busy_on_activation=true;
    if(fault==2) frame[100]^=1;
    if(fault==3) fail_lut_restore=true;
    assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))!=ESP_OK);
    if(fault==2) assert(command_count==0);
    unsigned activations=0;
    for(size_t i=0;i<command_count;++i) if(commands[i].command==0x20) ++activations;
    assert(activations<=1);
    if(fault==3) assert(activations==1 && d.faulted && !d.shadow_valid && !d.papermono_baseline_valid && !d.reference_awake && !d.controller_ready);
    assert(!d.black_hold_active && !d.black_hold_pending && epd_ssd1677_black_hold_blocked() && epd_ssd1677_black_hold_spent(c));
    stuck_busy=busy_on_activation=fail_lut_restore=false; fail_command=0;
    const size_t before=command_count;
    assert(epd_ssd1677_black_hold_arm(&d,EPD_SSD1677_BLACK_HOLD_B)==ESP_ERR_INVALID_STATE);
    assert(epd_ssd1677_black_hold_arm(&d,EPD_SSD1677_BLACK_HOLD_D)==ESP_ERR_INVALID_STATE);
    assert(command_count==before);
    d=make_display();
    assert(epd_ssd1677_black_hold_blocked() && epd_ssd1677_black_hold_spent(c));
    assert(epd_ssd1677_black_hold_arm(&d,EPD_SSD1677_BLACK_HOLD_BASELINE)==ESP_ERR_INVALID_STATE);
}
int main(void)
{
    unsigned decoded[4]; const uint8_t *luts[]={ssd1677_trial_black_hold_b_lut,ssd1677_trial_black_hold_d_lut};
    for(unsigned c=0;c<2;++c) {
        waveform_frames(luts[c],0,decoded);
        assert(decoded[0]==2 && decoded[1]==0 && decoded[2]==2 && decoded[3]==6);
        assert(-15*(int)decoded[2]+5*(int)decoded[3]==0);
        assert(!memcmp(luts[c]+10,ssd1677_reference_lut+10,100));
    }
    epd_ssd1677_baseline_main();
    for(unsigned r=0;r<2;++r) for(unsigned s=0;s<2;++s) for(unsigned o=0;o<2;++o) test_identical_bd(r,s,o);
    for(unsigned c=EPD_SSD1677_BLACK_HOLD_B;c<=EPD_SSD1677_BLACK_HOLD_D;++c) {
        for(unsigned f=0;f<4;++f) test_failure((epd_ssd1677_black_hold_t)c,f);
        fresh_boot(); epd_ssd1677_t d=prepared(false);
        assert(epd_ssd1677_black_hold_arm(&d,(epd_ssd1677_black_hold_t)c)==ESP_OK);
        epd_ssd1677_black_hold_cancel(&d);
        assert(!d.black_hold_pending && epd_ssd1677_black_hold_spent((epd_ssd1677_black_hold_t)c) && !epd_ssd1677_black_hold_blocked());
        epd_ssd1677_black_hold_t other=c==EPD_SSD1677_BLACK_HOLD_B?EPD_SSD1677_BLACK_HOLD_D:EPD_SSD1677_BLACK_HOLD_B;
        assert(epd_ssd1677_black_hold_arm(&d,other)==ESP_OK); epd_ssd1677_black_hold_cancel(&d);
        assert(epd_ssd1677_set_controller_mode(&d,"fastest-waveform=black-hold-single")==ESP_ERR_INVALID_ARG);
        assert(epd_ssd1677_set_controller_mode(&d,"fastest-waveform=hold")==ESP_ERR_INVALID_ARG);
    }
    puts("black-single B/D: SPI differs only LUT byte0; R0/R2 retain/swap, independent budgets, cancel, fault/restore gates PASS");
    return 0;
}
