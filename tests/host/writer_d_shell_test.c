#include <stdarg.h>
#include <stdlib.h>
#define SSD1677_DIRTY_TEST_MAIN shell_dirty_fixture_main
#include "epd_ssd1677_dirty_hold_test.c"
#include "../../src/services/solar_os_display_submission.h"

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
    char *args[]={"display","test","display0","writer-lut",(char *)mode};
    used=0; output[0]=0; command_count=0;
    display_cmd_writer_lut(&io,5,args);
    assert(!command_count); /* EVERY command, including rejected selects, is RAM-only. */
}
int main(void)
{
    device=make_display(); device.papermono_otp=true; device.fastest_tiles=true;
    device.fastest_spi_hz=20000000; device.band_buffer=band;
    assert(epd_ssd1677_set_controller_mode(&device,"refresh=fastest")==ESP_OK);
    assert(epd_ssd1677_set_controller_mode(&device,"dirty-window=rows-dual")==ESP_OK);
    device.execution_context=&device; device.operation_begin=begin; device.operation_end=end;
    command("status",2); assert(strstr(output,"mode=A attempt=0/6"));
    command("candidate_d",2); assert(device.writer_trial.enabled && strstr(output,"mode=D"));
    command("disable",2); assert(!device.writer_trial.enabled);
    command("candidate_d",1); assert(!device.writer_trial.enabled && strstr(output,"USB:"));
    command("unknown",2); assert(strstr(output,"err="));
    command("trace",2); assert(strstr(output,"queue submitted="));
    atomic_store(&writer_d_attempts,6);
    command("candidate_d",2); assert(!device.writer_trial.enabled && strstr(output,"cap=1"));
    puts("writer_d_shell_test: USB gating, controls/status/trace, no physical updates, cap PASS");
    return 0;
}
