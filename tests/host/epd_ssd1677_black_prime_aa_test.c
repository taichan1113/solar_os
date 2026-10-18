/* Reuse actual Driver/SPI and the existing retain/swap RAM models. */
#define BLACK_REPEAT3_DRIVER_MAIN previous_repeat3_fixture_main
#include "epd_ssd1677_black_repeat3_test.c"
#undef BLACK_REPEAT3_DRIVER_MAIN

static epd_ssd1677_black_hold_t sequence(unsigned step)
{ return step==0 ? EPD_SSD1677_BLACK_HOLD_D : EPD_SSD1677_BLACK_HOLD_A; }

static void test_sequence(bool r2,bool swap,bool resync)
{
    fresh_boot();epd_ssd1677_t d=prepared(r2);
    assert(!epd_ssd1677_black_hold_attempts(EPD_SSD1677_BLACK_HOLD_A));
    assert(!epd_ssd1677_black_hold_attempts(EPD_SSD1677_BLACK_HOLD_D));
    command_count=0;
    assert(epd_ssd1677_black_hold_arm(&d,EPD_SSD1677_BLACK_HOLD_A)==ESP_ERR_INVALID_STATE);
    assert(epd_ssd1677_black_hold_arm(&d,EPD_SSD1677_BLACK_HOLD_B)==ESP_ERR_INVALID_ARG);
    assert(!command_count);
    controller_ram_t *m=malloc(sizeof(*m));assert(m);controller_seed(m,&d);m->swap_after_activation=swap;
    captured_command_t *reference=malloc(sizeof(commands));assert(reference);size_t count=0;
    if(resync)d.dirty_hold_dual_updates=31;
    for(unsigned step=0;step<3;++step) {
        command_count=0;assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK && !command_count);
        assert(epd_ssd1677_black_hold_arm(&d,sequence(step))==ESP_OK);
        check_step(&d,sequence(step),m);
        assert(epd_ssd1677_black_hold_attempts(EPD_SSD1677_BLACK_HOLD_D)==1);
        assert(epd_ssd1677_black_hold_attempts(EPD_SSD1677_BLACK_HOLD_A)==step);
        assert(epd_ssd1677_black_hold_arm(&d,EPD_SSD1677_BLACK_HOLD_D)==ESP_ERR_INVALID_STATE);
        assert(epd_ssd1677_black_hold_arm(&d,EPD_SSD1677_BLACK_HOLD_BASELINE)==ESP_ERR_INVALID_STATE);
        if(resync)assert(d.timing.writes==(step==1?4:3));
        if(!step){memcpy(reference,commands,sizeof(commands));count=command_count;}
        else if(!resync) {
            assert(count==command_count);unsigned luts=0;
            for(size_t i=0;i<count;++i) {
                assert(reference[i].command==commands[i].command && reference[i].length==commands[i].length);
                if(commands[i].command==0x32 && luts++==0) {
                    assert(commands[i].data[0]==0 && commands[i].data[1]==0);
                    assert(reference[i].data[0]==0x2f && reference[i].data[1]==0xf0);
                    assert(!memcmp(reference[i].data+2,commands[i].data+2,103));
                } else assert(!memcmp(reference[i].data,commands[i].data,reference[i].length));
            }
        }
    }
    assert(epd_ssd1677_black_hold_spent(EPD_SSD1677_BLACK_HOLD_A));
    assert(epd_ssd1677_black_hold_spent(EPD_SSD1677_BLACK_HOLD_D));
    size_t before=command_count;
    assert(epd_ssd1677_black_hold_arm(&d,EPD_SSD1677_BLACK_HOLD_A)==ESP_ERR_INVALID_STATE && before==command_count);
    d=make_display(); /* real display/controller state rebuilt, static budget retained */
    assert(epd_ssd1677_black_hold_attempts(EPD_SSD1677_BLACK_HOLD_D)==1);
    assert(epd_ssd1677_black_hold_attempts(EPD_SSD1677_BLACK_HOLD_A)==2);
    assert(epd_ssd1677_black_hold_spent(EPD_SSD1677_BLACK_HOLD_A) && epd_ssd1677_black_hold_spent(EPD_SSD1677_BLACK_HOLD_D));
    free(reference);free(m);
}
static void test_interruption(unsigned step,unsigned fault,bool cancel)
{
    fresh_boot();epd_ssd1677_t d=prepared(false);
    controller_ram_t *m=malloc(sizeof(*m));assert(m);controller_seed(m,&d);
    for(unsigned i=0;i<step;++i) { assert(epd_ssd1677_black_hold_arm(&d,sequence(i))==ESP_OK);check_step(&d,sequence(i),m); }
    assert(epd_ssd1677_black_hold_arm(&d,sequence(step))==ESP_OK);
    command_count=0;
    if(cancel) epd_ssd1677_black_hold_cancel(&d);
    else {
        if(fault==0)fail_command=0x32;
        if(fault==1)busy_on_activation=true;
        if(fault==2)fail_lut_restore=true;
        if(fault==3)frame[9]^=1;
        assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))!=ESP_OK);
    }
    assert(epd_ssd1677_black_hold_attempts(EPD_SSD1677_BLACK_HOLD_D)==1);
    assert(epd_ssd1677_black_hold_attempts(EPD_SSD1677_BLACK_HOLD_A)==step);
    assert(epd_ssd1677_black_hold_blocked() && !d.black_hold_pending && !d.black_hold_active);
    unsigned activations=0;for(size_t i=0;i<command_count;++i)if(commands[i].command==0x20)++activations;
    assert(activations<=1 && (!cancel || !activations));
    size_t before=command_count;
    assert(epd_ssd1677_black_hold_arm(&d,EPD_SSD1677_BLACK_HOLD_D)==ESP_ERR_INVALID_STATE);
    assert(epd_ssd1677_black_hold_arm(&d,EPD_SSD1677_BLACK_HOLD_A)==ESP_ERR_INVALID_STATE && command_count==before);
    d=make_display();assert(epd_ssd1677_black_hold_blocked());free(m);
}
int main(void)
{
    assert(!memcmp(ssd1677_trial_black_hold_d_lut+2,ssd1677_reference_lut+2,108));
    for(unsigned r=0;r<2;++r)for(unsigned s=0;s<2;++s)test_sequence(r,s,false);
    test_sequence(true,true,true);
    for(unsigned step=0;step<3;++step) {
        test_interruption(step,0,true);
        for(unsigned fault=0;fault<4;++fault)test_interruption(step,fault,false);
    }
    fresh_boot();epd_ssd1677_t d=prepared(false);command_count=0;
    assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK && !command_count);
    frame[7]^=1;assert(epd_ssd1677_present_snapshot(&d,frame,sizeof(frame))==ESP_OK);
    expect_data(0x32,ssd1677_reference_lut,105);
    puts("prime-AA Driver: fixed D/A/A, one D, fourth/reinit/preparation refusal, cancel/fault stop, identical RAM/voltages, R0/R2 retain/swap and periodic resync PASS");
    return 0;
}
