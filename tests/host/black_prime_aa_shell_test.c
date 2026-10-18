/* Existing real Shell/U8g2/Driver/SPI fixture; previous B/D main is not run. */
#define BLACK_SINGLE_SHELL_MAIN previous_bd_shell_fixture_main
#include "black_single_shell_test.c"
#undef BLACK_SINGLE_SHELL_MAIN

static void run_prime(void)
{
    solar_os_shell_io_t io={.port=2};
    char *args[]={"display","test","display0","black-hold-d-prime-aa"};
    display_cmd_black_hold_single(&io,4,args);
}
static unsigned activation_total(void)
{
    unsigned total=0;const char *p=output;
    while((p=strstr(p,"BH phase="))) {const char *n=strstr(p,"cmd20=");assert(n);total+=(unsigned)strtoul(n+6,NULL,10);++p;}
    return total;
}
static void check_mark(const char *name,int64_t anchor,int64_t delay)
{
    char prefix[40],found[24];long long a,target,actual;
    snprintf(prefix,sizeof(prefix),"BH mark=%s ",name);const char *p=strstr(output,prefix);assert(p);
    assert(sscanf(p,"BH mark=%23s anchor_us=%lld target_us=%lld actual_us=%lld",found,&a,&target,&actual)==4);
    assert(!strcmp(found,name) && a==anchor && target==anchor+delay && actual>=target);
    assert(actual-target<(long long)SOLAR_OS_EPD_TEST_TICK_MS*1000);
}
static void check_schedule(void)
{
    check_mark("T0",prepared_end_us,5000000);
    int64_t previous=prepared_end_us;
    for(unsigned step=1;step<=3;++step) {
        char prefix[128];snprintf(prefix,sizeof(prefix),"BH schedule condition=DAA step=%u ",step);
        const char *p=strstr(output,prefix);assert(p);unsigned found;long long old,goal,requested,planned;
        assert(sscanf(p,"BH schedule condition=DAA step=%u previous_end_us=%lld goal_us=%lld requested_us=%lld planned_us=%lld",&found,&old,&goal,&requested,&planned)==5);
        assert(found==step && old==previous && goal==previous+60000000);
        assert(planned==(goal>requested+5000000?goal:requested+5000000));
        char mark[16];snprintf(mark,sizeof(mark),"measure%u",step);check_mark(mark,planned,0);
        assert(repeat_request_us[step-1]>=planned && repeat_request_us[step-1]-previous>=60000000);
        snprintf(mark,sizeof(mark),"T%u",step);check_mark(mark,repeat_end_us[step-1],2000000);
        snprintf(prefix,sizeof(prefix),"BH phase=DAA-repeat-%u err=0 candidate=%u",step,step==1?2:3);assert(strstr(output,prefix));
        snprintf(prefix,sizeof(prefix),"BH lut phase=DAA-repeat-%u requested_lut0=%s lut_send_ok=1 baseline_restore_ok=1",step,step==1?"2FF0":"0000");assert(strstr(output,prefix));
        previous=repeat_end_us[step-1];
    }
    assert(strstr(output,"completed=0 boot_attempts=0 measured_cmd20=0"));
    assert(strstr(output,"completed=3 boot_attempts=3 measured_cmd20=3"));
    assert(strstr(output,"diagnostic_cmd20=10") && strstr(output,"ram_bytes=192000"));
}
int main(int argc,char **argv)
{
    const char *mode=argc>1?argv[1]:"normal";
    const bool r2=!strcmp(mode,"r2"),delay=!strcmp(mode,"delay"),clock=!strcmp(mode,"clock");
    const bool fail=!strncmp(mode,"fail",4),restore=!strncmp(mode,"restore",7),submit=!strncmp(mode,"submit",6),abort=!strncmp(mode,"abort",5),stop=!strncmp(mode,"stop",4);
    const unsigned step=(fail||restore||submit||abort)?(unsigned)(mode[strlen(mode)-1]-'0'):0;
    setup(r2);submission_failure_at=0;gate_delay_us=delay?70000000:0;
    if(clock)time_us=1000000000000LL;
    solar_os_shell_io_t io={.port=2},nonport={.port=0};
    char *bad[]={"display","test","display0","black-hold-d-prime-aa","D"};
    display_cmd_black_hold_single(&io,5,bad);display_cmd_black_hold_single(&nonport,4,bad);
    char *old[]={"display","test","display0","black-hold-repeat3"};display_cmd_black_hold_single(&io,4,old);
    fixture.dirty_hold_full_width=false;run_prime();assert(!submitted && !claims);fixture.dirty_hold_full_width=true;
    assert(!submitted && !claims);
    keys="ggggg";
    if(fail)failure_at=3+step;
    if(restore)restore_failure_at=3+step;
    if(submit)submission_failure_at=3+step;
    if(abort){abort_wait=true;abort_submission_at=2+step;}
    if(stop) {
        if(!strcmp(mode,"stop-white"))keys="q";
        else if(!strcmp(mode,"stop0"))keys="gq";
        else if(!strcmp(mode,"stop1"))keys="ggq";
        else if(!strcmp(mode,"stop2"))keys="gggq";
        else assert(false);
    }
    run_prime();assert(claims==1 && releases==1 && !fixture.black_hold_pending && !fixture.black_hold_active);
    if(fail||restore||submit) {
        assert(step>=1 && step<=3 && submitted==3+step && !redraws);
        assert(epd_ssd1677_black_hold_blocked());
        assert(epd_ssd1677_black_hold_attempts(EPD_SSD1677_BLACK_HOLD_D)==1);
        assert(epd_ssd1677_black_hold_attempts(EPD_SSD1677_BLACK_HOLD_A)==step-1);
    } else if(abort) {
        assert(step>=1 && step<=3 && fast_updates==step && otp_updates==3 && redraws==1);
        assert(activation_total()==step+6 && epd_ssd1677_black_hold_blocked());
        assert(epd_ssd1677_black_hold_attempts(EPD_SSD1677_BLACK_HOLD_D)==1);
        assert(epd_ssd1677_black_hold_attempts(EPD_SSD1677_BLACK_HOLD_A)==step-1);
    } else if(stop) {
        const unsigned n=!strcmp(mode,"stop2")?2:!strcmp(mode,"stop1")?1:0;
        const bool white=!strcmp(mode,"stop-white");
        assert(activation_total()==(white?4:n+7) && redraws==1);
        assert(fast_updates==(white?0:n+1) && otp_updates==(white?2:3));
        assert(epd_ssd1677_black_hold_attempts(EPD_SSD1677_BLACK_HOLD_D)==(n?1:0));
        assert(epd_ssd1677_black_hold_attempts(EPD_SSD1677_BLACK_HOLD_A)==(n>1?n-1:0));
    } else {
        assert(submitted==7 && fast_updates==4 && otp_updates==3 && gate_reads==5 && redraws==1 && !*keys);
        assert(activation_total()==10);check_schedule();
        assert(epd_ssd1677_black_hold_attempts(EPD_SSD1677_BLACK_HOLD_D)==1);
        assert(epd_ssd1677_black_hold_attempts(EPD_SSD1677_BLACK_HOLD_A)==2);
    }
    assert(fixture.configured_spi_hz==10000000 && !fixture.timing_enabled);
    unsigned before=submitted;run_prime();assert(submitted==before);
    setup(r2);run_prime();assert(!submitted && !claims); /* session restart/reset cannot refund */
    puts("prime-AA Shell: fixed real SPI D/A/A, explicit gates, common 60s goal/late approval, bounded logs, no intermediate OTP, cancel/fault stop and no restart PASS");
    return 0;
}
