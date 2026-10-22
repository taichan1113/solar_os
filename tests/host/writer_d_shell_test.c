#include <stdarg.h>
#include <stdlib.h>
#define SSD1677_DIRTY_TEST_MAIN shell_dirty_fixture_main
#include "epd_ssd1677_dirty_hold_test.c"
#include "../../src/services/solar_os_display_submission.h"

#define SOLAR_OS_SHELL_IO_KIND_TERMINAL 1
#define SOLAR_OS_SHELL_IO_KIND_PORT 2
typedef struct { int port; } solar_os_shell_io_t;
typedef struct { bool ready; u8g2_t *u8g2; const char *driver; } solar_os_display_target_t;
static epd_ssd1677_t device;
static char output[65536]; static size_t used;
static int solar_os_shell_io_kind(solar_os_shell_io_t *io) { return io->port; }
static void solar_os_shell_io_printf(solar_os_shell_io_t *io,const char *fmt,...)
{
    (void)io; va_list args; va_start(args,fmt);
    const int n=vsnprintf(output+used,sizeof(output)-used,fmt,args); va_end(args);
    assert(n>=0 && (size_t)n<sizeof(output)-used); used+=(size_t)n;
}
static void solar_os_shell_io_writeln(solar_os_shell_io_t *io,const char *s)
{ solar_os_shell_io_printf(io,"%s\n",s); }
static bool solar_os_display_find_target(const char *name,solar_os_display_target_t *t)
{ *t=(solar_os_display_target_t){.ready=true,.u8g2=&device.u8g2,.driver="ssd1677"}; return !strcmp(name,"display0"); }
static esp_err_t solar_os_display_submission_status(u8g2_t *g,solar_os_display_submission_status_t *s)
{ (void)g; *s=(solar_os_display_submission_status_t){0}; return ESP_OK; }
static void begin(void *p,epd_ssd1677_operation_t op) { (void)p; assert(op==EPD_SSD1677_OP_READ); }
static void end(void *p,epd_ssd1677_operation_t op,esp_err_t e) { (void)p; (void)e; assert(op==EPD_SSD1677_OP_READ); }
#include "../../src/shell/solar_os_shell_writer_lut.inc"
static void command(const char *mode,int port)
{
    solar_os_shell_io_t io={port};
    char *args[]={"display","test","display0","fastest-lut",(char *)mode};
    used=0; output[0]=0; command_count=0;
    display_cmd_writer_lut(&io,5,args);
    assert(!command_count); /* EVERY command, including rejected selects, is RAM-only. */
}
static void test_ram_commands(void)
{
    for(int port=1;port<=2;port++) {
        solar_os_shell_io_t io={port};
        for(unsigned lut=0;lut<5;lut++) {
            char number[2]={(char)('1'+lut),0}; char *l[]={"lut",number};
            used=0; output[0]=0; command_count=0; display_cmd_lut(&io,2,l);
            const bool a=device.writer_trial.baseline_selected;
            const bool e1=device.writer_trial.e1_selected,sd=device.writer_trial.sketch_d_selected,f=device.writer_trial.f_selected;
            const char *modes[]={"rows","full","status","rows"};
            for(unsigned i=0;i<4;i++) {
                char *args[]={"ram",(char *)modes[i]}; used=0; output[0]=0;command_count=0;
                display_cmd_ram(&io,2,args);
                assert(!strcmp(output,(i==1 || i==2)?"RAM: full\n":"RAM: rows-dual\n"));
                assert(!command_count && device.writer_trial.baseline_selected==a &&
                    device.writer_trial.e1_selected==e1 && device.writer_trial.sketch_d_selected==sd && device.writer_trial.f_selected==f);
            }
        }
        char *args[]={"ram","unknown","extra"};
        used=0;output[0]=0;command_count=0; display_cmd_ram(&io,2,args);
        assert(strstr(output,"usage:") && !device.dirty_hold_force_full && !command_count);
        used=0;output[0]=0;display_cmd_ram(&io,3,args);assert(strstr(output,"usage:"));
        used=0;output[0]=0;display_cmd_ram(&io,1,args);assert(!strcmp(output,"RAM: rows-dual\n"));
    }
    solar_os_shell_io_t invalid={0}; char *args[]={"ram","full"};
    used=0;output[0]=0;command_count=0;display_cmd_ram(&invalid,2,args);
    assert(strstr(output,"usage:") && !device.dirty_hold_force_full && !command_count);
    puts("RAM device/USB Shell: one-line rows/full/status, 5 LUT selections independent, invalid args/IO and selector SPI=0 PASS");
}
int main(void)
{
    char invalid_state[32]; snprintf(invalid_state,sizeof(invalid_state),"err=%d",ESP_ERR_INVALID_STATE);
    device=make_display(); device.papermono_otp=true; device.fastest_tiles=true;
    device.fastest_spi_hz=20000000; device.band_buffer=band;
    assert(epd_ssd1677_set_controller_mode(&device,"refresh=fastest")==ESP_OK);
    assert(epd_ssd1677_set_controller_mode(&device,"dirty-window=rows-dual")==ESP_OK);
    device.execution_context=&device; device.operation_begin=begin; device.operation_end=end;
    command("status",2); assert(strstr(output,"mode=D scope=all-apps-except-sketch attempts=0"));
    command("baseline",2); assert(device.writer_trial.baseline_selected && strstr(output,"mode=A"));
    command("candidate_d",2); assert(!device.writer_trial.baseline_selected && strstr(output,"mode=D"));
    command("candidate_e1",2); assert(device.writer_trial.e1_selected && strstr(output,"mode=E1 scope=all-apps"));
    device.writer_trial.status.e1_physical=3; device.writer_trial.status.e1_success=3;
    command("status",2); assert(strstr(output,"E10x20=3") && strstr(output,"physical=3"));
    device.writer_trial.records[0]=(epd_ssd1677_writer_record_t){.candidate_e1=true,.origin.is_sketch=true,.physical=1};
    device.writer_trial.status.trace_count=device.writer_trial.status.trace_next=1;
    command("trace",2); assert(strstr(output,"source=sketch mode=E1 0x20=1"));
    command("candidate_f",2); assert(device.writer_trial.f_selected && !device.writer_trial.e1_selected && strstr(output,"mode=candidate_f scope=all-apps"));
    device.writer_trial.status.f_physical=2; device.writer_trial.status.f_success=2;
    command("status",2); assert(strstr(output,"F0x20=2") && strstr(output,"display physical=5"));
    device.writer_trial.records[0]=(epd_ssd1677_writer_record_t){.candidate_f=true,.origin.is_sketch=true,.physical=1};
    command("trace",2); assert(strstr(output,"source=sketch mode=candidate_f 0x20=1"));
    command("candidate_d",2); assert(!device.writer_trial.e1_selected && strstr(output,"mode=D scope=all-apps-except-sketch"));
    command("candidate_d_sketch",2); assert(device.writer_trial.sketch_d_selected &&
        strstr(output,"mode=D scope=all-apps") && strstr(output,"sketch_d_trial=1"));
    command("trial-status",2); assert(strstr(output,"trial started=0 active=0"));
    command("trial-stop",2); assert(strstr(output,invalid_state));
    command("trial-start",2); assert(strstr(output,"trial started=1 active=1"));
    command("trial-start",2); assert(strstr(output,invalid_state) && device.writer_trial.status.trial.active);
    command("trial-stop",2); assert(strstr(output,"trial started=1 active=0") &&
        strstr(output,"activation_start_to_start") && strstr(output,"busy_ready_to_next_start") && strstr(output,"queue valid=0"));
    command("trace",2); assert(strstr(output,"WL diag seq=") && strstr(output,"ram_window="));
    command("candidate_d",2); assert(!device.writer_trial.sketch_d_selected && strstr(output,"scope=all-apps-except-sketch"));
    command("disable",2); assert(!!device.writer_trial.baseline_selected);
    assert(!device.writer_trial.e1_selected);
    for (int port = 1; port <= 2; port++) {
        solar_os_shell_io_t io = {port};
        const char *choices[] = {"1", "2", "3", "4", "5"};
        for (unsigned i = 0; i < 5; i++) {
            char *args[] = {"lut", (char *)choices[i]};
            used = 0; output[0] = 0; command_count = 0;
            display_cmd_lut(&io, 2, args);
            assert(!command_count && !strcmp(output,i==0?"LUT: A\n":i==3?"LUT: E1\n":i==4?"LUT: F\n":"LUT: D\n"));
            assert(device.writer_trial.baseline_selected == (i == 0));
            assert(device.writer_trial.sketch_d_selected == (i == 2));
            assert(device.writer_trial.e1_selected == (i == 3));
            assert(device.writer_trial.f_selected == (i == 4));
        }
        char *args[] = {"lut", "invalid"};
        used = 0; output[0] = 0;
        display_cmd_lut(&io, 2, args);
        assert(strstr(output, "usage:") && device.writer_trial.f_selected);
        display_cmd_lut(&io, 1, args);
        assert(strstr(output, "mode=candidate_f") && device.writer_trial.f_selected);
        command("candidate_d_sketch",port);
        assert(device.writer_trial.sketch_d_selected && strstr(output,"err=0"));
    }
    command("baseline",0); assert(!device.writer_trial.baseline_selected);
    command("unknown",2); assert(strstr(output,"err="));
    command("trace",2); assert(strstr(output,"queue submitted=") && strstr(output,"display physical="));
    device.writer_trial.status.attempts=1000;
    command("candidate_d",2); assert(!device.writer_trial.baseline_selected && strstr(output,"attempts=1000"));
    device.writer_trial.status.records=UINT64_MAX;
    device.writer_trial.status.trace_count=0;
    command("trace",2); assert(strstr(output,"records=18446744073709551615"));
    test_ram_commands();
    puts("writer_d_shell_test: device Shell/USB five-mode selection, global controls/status/trace, no physical updates, default D and no cumulative cap PASS");
    return 0;
}
