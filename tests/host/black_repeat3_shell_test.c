/* Reuse the real diagnostic/U8g2/Driver fixture; old B/D main is not executed. */
#define BLACK_SINGLE_SHELL_MAIN black_bd_fixture_main
#include "black_single_shell_test.c"
#undef BLACK_SINGLE_SHELL_MAIN

static void run_repeat(char c)
{
    solar_os_shell_io_t io={.port=2};char condition[2]={c,0};
    char *args[]={"display","test","display0","black-hold-repeat3",condition};
    display_cmd_black_hold_single(&io,5,args);
}
static void check_mark(const char *name,int64_t anchor,int64_t delay)
{
    char prefix[40],found[24];long long a,target,actual;
    snprintf(prefix,sizeof(prefix),"BH mark=%s ",name);
    const char *line=strstr(output,prefix);assert(line);
    assert(sscanf(line,"BH mark=%23s anchor_us=%lld target_us=%lld actual_us=%lld",found,&a,&target,&actual)==4);
    assert(!strcmp(found,name) && a==anchor && target-a==delay);
    assert(actual>=target && actual-target<(long long)SOLAR_OS_EPD_TEST_TICK_MS*1000);
}
static unsigned audit_activations(void)
{
    unsigned total=0;const char *p=output;
    while((p=strstr(p,"BH phase="))) {
        const char *n=strstr(p,"cmd20=");assert(n);total+=(unsigned)strtoul(n+6,NULL,10);++p;
    }
    return total;
}
static void check_success(char c)
{
    assert(submitted==7 && fast_updates==4 && otp_updates==3 && gate_reads==5);
    assert(claims==1 && releases==1 && redraws==1 && !*keys && audit_activations()==10);
    assert(fixture.configured_spi_hz==10000000 && !fixture.timing_enabled);
    assert(!fixture.black_hold_active && !fixture.black_hold_pending);
    check_mark("T0",prepared_end_us,5000000);
    for(unsigned step=1;step<=3;++step) {
        char prefix[64],condition;unsigned found,attempts;long long requested;
        snprintf(prefix,sizeof(prefix),"BH request condition=%c step=%u ",c,step);
        const char *p=strstr(output,prefix);assert(p);
        assert(sscanf(p,"BH request condition=%c step=%u requested_us=%lld boot_attempts=%u",&condition,&found,&requested,&attempts)==4);
        assert(condition==c && found==step && attempts==step);
        char mark[16];snprintf(mark,sizeof(mark),"measure%u",step);check_mark(mark,requested,5000000);
        assert(repeat_request_us[step-1]>=requested+5000000);
        snprintf(mark,sizeof(mark),"T%u",step);check_mark(mark,repeat_end_us[step-1],2000000);
        snprintf(prefix,sizeof(prefix),"BH phase=%c-repeat-%u err=0",c,step);assert(strstr(output,prefix));
    }
    assert(strstr(output,"completed=0 boot_attempts=0 measured_cmd20=0"));
    assert(strstr(output,"completed=3 boot_attempts=3 measured_cmd20=3"));
    assert(strstr(output,"diagnostic_cmd20=10"));
}
int main(int argc,char **argv)
{
    const char *mode=argc>1?argv[1]:"normal";
    const bool fail=!strncmp(mode,"fail",4),restore=!strncmp(mode,"restore",7),submit=!strncmp(mode,"submit",6);
    const bool abort=!strncmp(mode,"abort",5),stop1=!strncmp(mode,"stop1",5),stop2=!strncmp(mode,"stop2",5),cancel=!strncmp(mode,"cancel",6);
    const bool r2=!strcmp(mode,"r2");
    const char c=(fail||restore||submit||abort||stop1||stop2||cancel)?(strchr(mode,'A')?'A':'D'):'A';
    const epd_ssd1677_black_hold_t selected=c=='A'?EPD_SSD1677_BLACK_HOLD_A:EPD_SSD1677_BLACK_HOLD_D;
    const char other=c=='A'?'D':'A';
    setup(r2);submission_failure_at=0;gate_delay_us=!strcmp(mode,"delay")?7000000:0;
    memset(repeat_request_us,0,sizeof(repeat_request_us));memset(repeat_end_us,0,sizeof(repeat_end_us));
    run_repeat('B');run_repeat('C');run_repeat('E');assert(!submitted && !claims);
    fixture.dirty_hold_full_width=false;run_repeat(c);assert(!submitted && !claims);
    fixture.dirty_hold_full_width=true;
    solar_os_shell_io_t nonport={.port=0};char *bad[]={"display","test","display0","black-hold-repeat3","D"};
    display_cmd_black_hold_single(&nonport,5,bad);assert(!submitted && !claims);
    keys="ggggg";
    if(fail)failure_at=5;
    if(restore)restore_failure_at=5;
    if(submit)submission_failure_at=5;
    if(abort)abort_wait=true;
    if(cancel)keys="gq";
    if(stop1)keys="ggq";
    if(stop2)keys="gggq";
    run_repeat(c);
    assert(claims==1 && releases==1);
    if(fail||restore||submit) {
        assert(submitted==5 && !redraws && epd_ssd1677_black_hold_attempts(selected)==2);
        assert(!fixture.black_hold_pending && !fixture.black_hold_active);
        if(!submit)assert(fixture.faulted && epd_ssd1677_black_hold_blocked());
        unsigned before=submitted;run_repeat(c);run_repeat(other);assert(submitted==before);
        setup(r2);run_repeat(other);assert(!submitted && !claims);
        puts("repeat3 Shell: failed second update blocks both conditions, no cleanup/retry/next step PASS");return 0;
    }
    if(abort||cancel||stop1||stop2) {
        const unsigned count=stop2?2:stop1?1:0;
        assert(fast_updates==count+1 && otp_updates==3 && redraws==1 && audit_activations()==count+7);
        assert(epd_ssd1677_black_hold_attempts(selected)==(abort?1:count));
        assert(!fixture.black_hold_pending && !fixture.black_hold_active);
        if(abort)assert(strstr(output,"BH aborted wait=measure1"));
    } else check_success(c);
    unsigned before=submitted;run_repeat(c);assert(submitted==before); /* cannot reinitialize same session */
    setup(r2);submission_failure_at=0;keys="ggggg";gate_delay_us=0;
    run_repeat(other);check_success(other);
    before=submitted;run_repeat(c);run_repeat(other);assert(submitted==before);
    puts("repeat3 Shell: explicit per-update gates, 0..3 markers/attempts, stop/cancel, restore and no session restart PASS");return 0;
}
