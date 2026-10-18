/* Exercise the actual diagnostic, U8g2 image/font, and SSD1677 SPI payload. */
#include <stdarg.h>
#include <stdlib.h>
#define SSD1677_DIRTY_TEST_MAIN dirty_existing_main
#include "epd_ssd1677_dirty_hold_test.c"
#include "solar_os_fonts.h"

#define SOLAR_OS_DISPLAY_TARGET_OWNER_MAX 32
#define SOLAR_OS_MEMORY_EXTERNAL_REQUIRED 3
#define SOLAR_OS_DISPLAY_PRESENT_GRAPHICS 1
#define SOLAR_OS_SHELL_IO_KIND_PORT 2
typedef struct { int port; } solar_os_shell_io_t;
typedef struct {
    char name[16], driver[20];
    bool ready, black_is_one;
    u8g2_t *u8g2;
} solar_os_display_target_t;
typedef struct {
    uint64_t completed_id;
    esp_err_t error;
    bool busy, pending;
} solar_os_display_submission_status_t;

static epd_ssd1677_t fixture;
static unsigned submitted, fast_updates, otp_updates, claims, releases, gate_reads, redraws;
static unsigned failure_at, restore_failure_at;
#if SOLAR_OS_SSD1677_BLACK_HOLD_REPEAT3_TRIAL
static unsigned submission_failure_at;
static int64_t repeat_request_us[3], repeat_end_us[3], gate_delay_us;
#endif
static bool operator_gate, abort_wait;
static int64_t prepared_end_us, measured_end_us, measure_request_us;

static const char *keys;
static char output[32768];
static size_t output_len;
static uint8_t immutable[48000], previous[48000];
static esp_err_t last_error;

static int solar_os_shell_io_kind(solar_os_shell_io_t *io) { return io->port; }
static esp_err_t solar_os_shell_io_printf(solar_os_shell_io_t *io, const char *format, ...)
{
    (void)io;
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(output + output_len, sizeof(output) - output_len, format, ap);
    va_end(ap);
    assert(n >= 0 && (size_t)n < sizeof(output) - output_len);
    output_len += (size_t)n;
    if (!strncmp(format, "BH gate=", 8)) operator_gate = true;
    return ESP_OK;
}
static void solar_os_shell_io_writeln(solar_os_shell_io_t *io, const char *s)
{ (void)solar_os_shell_io_printf(io, "%s\n", s); }
static void solar_os_shell_io_flush(solar_os_shell_io_t *io) { (void)io; }
static esp_err_t solar_os_port_read(int *port, uint8_t *key, size_t len,
                                   unsigned timeout, size_t *n)
{
    assert(*port == 2 && len == 1 && timeout == 0);
    if (!operator_gate) {
        *n = 0;
        if (abort_wait && submitted == 3 && time_us >= prepared_end_us + 7500000) {
            *key = 'q'; *n = 1; abort_wait = false;
        }
        return ESP_OK;
    }
    assert(*keys);
#if SOLAR_OS_SSD1677_BLACK_HOLD_REPEAT3_TRIAL
    time_us += gate_delay_us; /* operator response latency, not Driver work */
#endif
    *key = (uint8_t)*keys++;
    *n = 1;
    gate_reads++;
    operator_gate = false;
    return ESP_OK;
}
static bool solar_os_display_find_target(const char *name, solar_os_display_target_t *t)
{
    *t = (solar_os_display_target_t){.ready=true, .u8g2=&fixture.u8g2};
    strcpy(t->name, "display0"); strcpy(t->driver, "ssd1677");
    return !strcmp(name, "display0");
}
static esp_err_t solar_os_display_claim(const char *name, const char *owner, char *busy, size_t n)
{ assert(!strcmp(name, "display0") && !strcmp(owner, "shell:black-single") && n); *busy=0; claims++; return ESP_OK; }
static esp_err_t solar_os_display_release(const char *name, const char *owner)
{ (void)name; (void)owner; releases++; return ESP_OK; }
static bool solar_os_sessions_foreground_is_shell(void) { return true; }
static void solar_os_sessions_mark_foreground_dirty(void) { redraws++; }
static esp_err_t solar_os_display_set_controller_mode(const char *name, const char *mode)
{ assert(!strcmp(name, "display0")); return epd_ssd1677_set_controller_mode(&fixture, mode); }
static esp_err_t solar_os_display_wait(u8g2_t *g, unsigned timeout)
{ assert(g==&fixture.u8g2 && timeout==70000); return last_error; }
static esp_err_t solar_os_display_submission_status(u8g2_t *g, solar_os_display_submission_status_t *s)
{ assert(g==&fixture.u8g2); *s=(solar_os_display_submission_status_t){.completed_id=submitted,.error=last_error}; return ESP_OK; }


static esp_err_t solar_os_display_submit(u8g2_t *g, int mode, uint64_t *id)
{
    assert(g==&fixture.u8g2 && mode==1);
    *id=++submitted;
#if SOLAR_OS_SSD1677_BLACK_HOLD_REPEAT3_TRIAL
    if (submitted>=4 && submitted<=6) repeat_request_us[submitted-4]=time_us;
    if (submitted==submission_failure_at) { last_error=ESP_FAIL; return last_error; }
#endif
    if (submitted == 4) measure_request_us = time_us;
    memcpy(immutable,frame,sizeof(frame)); memcpy(previous,shadow,sizeof(shadow));
    controller_ram_t *m=malloc(sizeof(*m)); assert(m);
    controller_seed(m,&fixture);
    const bool forced=fixture.black_hold_pending!=0;
    command_count=0;
    if(submitted==failure_at) busy_on_activation=true;
    if(submitted==restore_failure_at) fail_lut_restore=true;
    last_error=epd_ssd1677_present_snapshot(&fixture,immutable,sizeof(immutable));
    if (submitted == 3) prepared_end_us = fixture.timing.end_us;
    if (submitted == 4) measured_end_us = fixture.timing.end_us;
#if SOLAR_OS_SSD1677_BLACK_HOLD_REPEAT3_TRIAL
    if (submitted>=4 && submitted<=6) repeat_end_us[submitted-4]=fixture.timing.end_us;
#endif
    if(last_error==ESP_OK) {
        assert(!memcmp(shadow,immutable,sizeof(shadow)) && fixture.snapshot==NULL);
        if(forced) {
            ++fast_updates;
            assert(controller_apply(m,&fixture).activations==1);
            assert(m->classes[0]>0 && m->classes[1]==0 && m->classes[2]==0 && m->classes[3]>0);
        } else { ++otp_updates; assert(fixture.timing.activations==2); }
    }
    free(m); return last_error;
}
#include "../../src/shell/solar_os_shell_black_hold_single.inc"

static void setup(bool r2)
{
    fixture=make_display(); fixture.papermono_otp=true; fixture.band_buffer=band;
    fixture.fastest_tiles=true; fixture.fastest_spi_hz=20000000;
    fixture.refresh_mode=EPD_SSD1677_REFRESH_FASTEST;
    fixture.dirty_hold_trial=fixture.dirty_hold_full_width=fixture.dirty_hold_dual_sync=true;
    fixture.configured_spi_hz=10000000;
    u8g2_SetupDisplay(&fixture.u8g2,ssd1677_u8x8_display_cb,u8x8_dummy_cb,u8x8_dummy_cb,u8x8_dummy_cb);
    u8g2_SetupBuffer(&fixture.u8g2,frame,60,u8g2_ll_hvline_vertical_top_lsb,r2?U8G2_R2:U8G2_R0);
    submitted=fast_updates=otp_updates=claims=releases=gate_reads=failure_at=restore_failure_at=redraws=0;
    output_len=0; output[0]=0; last_error=ESP_OK;
    keys="gg"; operator_gate=abort_wait=false;
    prepared_end_us=measured_end_us=measure_request_us=0;
    stuck_busy=busy_on_activation=fail_lut_restore=false; actual_spi_hz=10000000;
}
static void run(char condition)
{
    solar_os_shell_io_t io={.port=2}; char arg[2]={condition,0};
    char *argv[]={"display","test","display0","black-hold-single",arg};
    display_cmd_black_hold_single(&io,5,argv);
}
static void check_timing(void)
{
    const char *names[] = {"T0", "measure", "T1"};
    const long long anchors[] = {prepared_end_us, prepared_end_us, measured_end_us};
    const long long delays[] = {5000000, 10000000, 2000000};
    for (unsigned i=0; i<3; ++i) {
        char prefix[32], name[16];
        snprintf(prefix,sizeof(prefix),"BH mark=%s ",names[i]);
        const char *line=strstr(output,prefix); assert(line);
        long long anchor,target,actual;
        assert(sscanf(line,"BH mark=%15s anchor_us=%lld target_us=%lld actual_us=%lld",
                      name,&anchor,&target,&actual)==4);
        assert(!strcmp(name,names[i]) && anchor==anchors[i] && target-anchor==delays[i]);
        assert(actual>=target && actual-target<(long long)SOLAR_OS_EPD_TEST_TICK_MS*1000);
        if (i==1) assert(actual==measure_request_us);
    }
    assert(gate_reads==2 && !strstr(output,"BH gate=T0-photograph"));
}
static void check_trace(char condition)
{
    char prefix[64];
    snprintf(prefix,sizeof(prefix),"BH trace phase=%c-single ",condition);
    const char *line=strstr(output,prefix); assert(line);
    long long lut,activate,ready,restore_start,restore_end;
    assert(sscanf(line+strlen(prefix),"lut_ready_us=%lld activate_call_us=%lld busy_wait_end_us=%lld restore_start_us=%lld restore_end_us=%lld",
                  &lut,&activate,&ready,&restore_start,&restore_end)==5);
    assert(lut<=activate && activate<=ready && ready<=restore_start && restore_start<=restore_end);
    assert(restore_end<=measured_end_us && strstr(line,"busy_edge_measured=0"));
}
#ifndef BLACK_SINGLE_SHELL_MAIN
#define BLACK_SINGLE_SHELL_MAIN main
#endif
int BLACK_SINGLE_SHELL_MAIN(int argc,char **argv)
{
    const char *arg=argc>1?argv[1]:"normal";
    const bool fail=!strncmp(arg,"fail",4), restore=!strncmp(arg,"restore",7);
    const bool abort=!strncmp(arg,"abort",5), cancel=!strncmp(arg,"cancel",6);
    const bool r2=!strcmp(arg,"r2");
    const char first=(fail||restore||abort||cancel)?(strchr(arg,'B')?'B':'D'):'B';
    const char other=first=='B'?'D':'B';
    setup(r2);
    run('A'); run('C'); run('E'); assert(!submitted && !claims);
    solar_os_shell_io_t non_port={.port=0};
    char *invalid_io[]={"display","test","display0","black-hold-single","B"};
    display_cmd_black_hold_single(&non_port,5,invalid_io);
    assert(!submitted && !claims);
    if(fail) failure_at=4;
    if(restore) restore_failure_at=4;
    if(abort) abort_wait=true;
    if(cancel) keys="q";
    run(first);
    assert(claims==1 && releases==1);
    const epd_ssd1677_black_hold_t selection=first=='B'?EPD_SSD1677_BLACK_HOLD_B:EPD_SSD1677_BLACK_HOLD_D;
    if(cancel) {
        assert(submitted==2 && fast_updates==0 && otp_updates==2 && redraws==1);
        assert(!epd_ssd1677_black_hold_spent(selection));
    } else if(abort) {
        assert(submitted==4 && fast_updates==1 && otp_updates==3 && redraws==1);
        assert(strstr(output,"BH mark=T0 ") && strstr(output,"BH aborted wait=measure"));
        assert(!epd_ssd1677_black_hold_spent(selection));
    } else if(fail||restore) {
        assert(fixture.faulted && !redraws && submitted==4 && otp_updates==2);
        assert(!fixture.black_hold_pending && !fixture.black_hold_active && epd_ssd1677_black_hold_spent(selection));
        assert(epd_ssd1677_black_hold_blocked());
        unsigned count=submitted; run(first); run(other); assert(submitted==count);
        setup(r2); run(other); assert(!submitted && !claims); /* reset cannot resume Trial */
        puts("black-single B/D Shell: failure blocks both candidates with no retry PASS");
        return 0;
    } else {
        assert(submitted==5 && fast_updates==2 && otp_updates==3 && redraws==1 && !*keys);
        check_timing(); check_trace(first);
        assert(fixture.configured_spi_hz==10000000 && !fixture.timing_enabled);
        assert(epd_ssd1677_black_hold_spent(selection));
    }
    unsigned count=submitted; run(first); assert(submitted==count);
    setup(r2); run(other); /* same boot, independent candidate after success/cancel */
    assert(submitted==5 && fast_updates==2 && otp_updates==3 && redraws==1 && !*keys);
    check_timing(); check_trace(other);
    count=submitted; run(first); run(other); assert(submitted==count);
    assert(!fixture.black_hold_active && !fixture.black_hold_pending);
    assert(strstr(output,"cmd32=2 cmd20=1") && strstr(output,"BH gate=T1-photograph"));
    puts("black-single B/D Shell: explicit single starts, independent budgets, timed markers, cancel, trace and restore PASS");
    return 0;
}
