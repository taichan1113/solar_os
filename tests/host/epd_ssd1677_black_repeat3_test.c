/* Real Driver/SPI; the existing RAM model tests both retain and face swap. */
#define SSD1677_DIRTY_TEST_MAIN dirty_existing_main
#include "epd_ssd1677_dirty_hold_test.c"

static void fresh_boot(void)
{
    memset(black_hold_candidate_spent,0,sizeof(black_hold_candidate_spent));
    black_hold_failed=false; /* Host only; display reinit cannot do this. */
    stuck_busy=false; /* release the previous fault-injection fixture pin */
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
    expect_data(0x32,ssd1677_reference_lut,105);
    expect_data(0x22,(uint8_t[]){0xcc},1);
    return d;
}
static void check_step(epd_ssd1677_t *d,epd_ssd1677_black_hold_t c,controller_ram_t *m)
{
    command_count=0;
    assert(epd_ssd1677_present_snapshot(d,frame,sizeof(frame))==ESP_OK);
    assert(controller_apply(m,d).activations==1);
    expect_data(0x32,c==EPD_SSD1677_BLACK_HOLD_D?ssd1677_trial_black_hold_d_lut:ssd1677_reference_lut,105);
    assert(!memcmp(find_command(0x32,1)->data,ssd1677_reference_lut,105));
    expect_data(0x03,(uint8_t[]){0x17},1);
    expect_data(0x04,(uint8_t[]){0x46,0xa8,0x32},3);
    expect_data(0x2c,(uint8_t[]){0x30},1);
    expect_data(0x22,(uint8_t[]){0x0c},1);
    unsigned activate=0,lut=0;
    for(size_t i=0;i<command_count;++i) {
        if(commands[i].command==0x20) ++activate;
        if(commands[i].command==0x32) ++lut;
    }
    assert(activate==1 && lut==2 && d->timing.activations==1);
    assert(m->classes[0]>0 && m->classes[1]==0 && m->classes[2]==0 && m->classes[3]>0);
    assert(!memcmp(m->bw,m->red,sizeof(m->bw)) && !memcmp(d->shadow,frame,sizeof(frame)));
    assert(!d->black_hold_active && !d->black_hold_pending && d->dirty_hold_ram_synced);
}
static void test_three(bool r2,bool swap)
{
    fresh_boot();
    epd_ssd1677_t d=prepared(r2);
    assert(epd_ssd1677_black_hold_attempts(EPD_SSD1677_BLACK_HOLD_A)==0);
    assert(epd_ssd1677_black_hold_attempts(EPD_SSD1677_BLACK_HOLD_D)==0);
    assert(epd_ssd1677_black_hold_arm(&d,EPD_SSD1677_BLACK_HOLD_B)==ESP_ERR_INVALID_ARG);
    controller_ram_t *m=malloc(sizeof(*m));assert(m);
    captured_command_t *a=malloc(sizeof(commands));assert(a);size_t a_count=0;
    for(unsigned which=0;which<2;++which) {
        const epd_ssd1677_black_hold_t c=which?EPD_SSD1677_BLACK_HOLD_D:EPD_SSD1677_BLACK_HOLD_A;
        controller_seed(m,&d);m->swap_after_activation=swap;
        for(unsigned n=1;n<=3;++n) {
            command_count=0;
            assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK && !command_count);
            assert(epd_ssd1677_black_hold_arm(&d,c)==ESP_OK);
            assert(epd_ssd1677_black_hold_attempts(c)==n);
            assert(epd_ssd1677_black_hold_arm(&d,c)==ESP_ERR_INVALID_STATE);
            check_step(&d,c,m);
            if(!which && n==1) { memcpy(a,commands,sizeof(commands));a_count=command_count; }
            else {
                assert(command_count==a_count);unsigned luts=0;
                for(size_t k=0;k<command_count;++k) {
                    assert(a[k].command==commands[k].command && a[k].length==commands[k].length);
                    if(commands[k].command==0x32 && luts++==0 && which) {
                        assert(!memcmp(a[k].data+2,commands[k].data+2,103));
                        assert(commands[k].data[0]==0x2f && commands[k].data[1]==0xf0);
                    } else assert(!memcmp(a[k].data,commands[k].data,a[k].length));
                }
            }
        }
        size_t before=command_count;
        assert(epd_ssd1677_black_hold_spent(c));
        assert(epd_ssd1677_black_hold_arm(&d,c)==ESP_ERR_INVALID_STATE && command_count==before);
        d=prepared(r2); /* controller/display reinitialization cannot refund */
        assert(epd_ssd1677_black_hold_attempts(c)==3 && epd_ssd1677_black_hold_spent(c));
    }
    command_count=0;frame[7]^=1;
    assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK);
    expect_data(0x32,ssd1677_reference_lut,105);
    free(a);free(m);
}
static void test_fault(epd_ssd1677_black_hold_t c,unsigned fault)
{
    fresh_boot();epd_ssd1677_t d=prepared(false);
    assert(epd_ssd1677_black_hold_arm(&d,c)==ESP_OK);
    epd_ssd1677_black_hold_cancel(&d); /* reserve/cancel consumes attempt 1 */
    assert(epd_ssd1677_black_hold_attempts(c)==1);
    assert(epd_ssd1677_black_hold_arm(&d,c)==ESP_OK);
    if(fault==0) fail_command=0x32;
    if(fault==1) busy_on_activation=true;
    if(fault==2) fail_lut_restore=true;
    if(fault==3) frame[9]^=1;
    command_count=0;
    assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))!=ESP_OK);
    unsigned count=0;for(size_t i=0;i<command_count;++i) if(commands[i].command==0x20) ++count;
    assert(count<=1 && epd_ssd1677_black_hold_attempts(c)==2 && epd_ssd1677_black_hold_blocked());
    assert(!d.black_hold_pending && !d.black_hold_active);
    if(fault==2) assert(count==1 && d.faulted && !d.shadow_valid && !d.papermono_baseline_valid);
    const size_t before=command_count;
    assert(epd_ssd1677_black_hold_arm(&d,EPD_SSD1677_BLACK_HOLD_A)==ESP_ERR_INVALID_STATE);
    assert(epd_ssd1677_black_hold_arm(&d,EPD_SSD1677_BLACK_HOLD_D)==ESP_ERR_INVALID_STATE && command_count==before);
    d=make_display();assert(epd_ssd1677_black_hold_blocked() && epd_ssd1677_black_hold_attempts(c)==2);
}
static void test_periodic_resync(void)
{
    fresh_boot();epd_ssd1677_t d=prepared(true);
    controller_ram_t *m=malloc(sizeof(*m));assert(m);controller_seed(m,&d);m->swap_after_activation=true;
    d.dirty_hold_dual_updates=31;
    for(unsigned n=0;n<3;++n) {
        assert(epd_ssd1677_black_hold_arm(&d,EPD_SSD1677_BLACK_HOLD_D)==ESP_OK);
        check_step(&d,EPD_SSD1677_BLACK_HOLD_D,m);
        assert(d.timing.writes==(n==1?4:3)); /* RAM resync is not an extra 0x20 */
    }
    free(m);
}
#ifndef BLACK_REPEAT3_DRIVER_MAIN
#define BLACK_REPEAT3_DRIVER_MAIN main
#endif
int BLACK_REPEAT3_DRIVER_MAIN(void)
{
    for(unsigned r=0;r<2;++r) for(unsigned s=0;s<2;++s) test_three(r,s);
    for(unsigned c=0;c<2;++c) {
        epd_ssd1677_black_hold_t selected=c?EPD_SSD1677_BLACK_HOLD_D:EPD_SSD1677_BLACK_HOLD_A;
        for(unsigned f=0;f<4;++f) test_fault(selected,f);
        fresh_boot();epd_ssd1677_t d=prepared(false);
        for(unsigned n=1;n<=3;++n) {
            assert(epd_ssd1677_black_hold_arm(&d,selected)==ESP_OK);epd_ssd1677_black_hold_cancel(&d);
            assert(epd_ssd1677_black_hold_attempts(selected)==n);
        }
        assert(epd_ssd1677_black_hold_arm(&d,selected)==ESP_ERR_INVALID_STATE);
    }
    test_periodic_resync();
    puts("repeat3 Driver: A/D 0..3, fourth/reinit refusal, cancel/fault budgets, identical payload, R0/R2 retain/swap, periodic RAM resync PASS");
    return 0;
}
