#define main ft6336_lifecycle_unused_main
#include "ft6336_lifecycle_test.c"
#undef main
#include "../../src/shell/solar_os_shell_touchraw.c"
#include <stdarg.h>

static solar_os_shell_io_t io;
static bool owns=true, fail_output;
static char output[16384];
static size_t output_len;
solar_os_shell_io_t *solar_os_shell_command_io(solar_os_context_t *ctx)
{ (void)ctx;return &io; }
esp_err_t solar_os_port_get_info(const char *name,solar_os_port_info_t *p)
{ assert(!strcmp(name,"cdc0"));*p=(solar_os_port_info_t){.claimed=true};strcpy(p->owner,"session:16");return ESP_OK; }
bool solar_os_port_shell_context_owns_session(const solar_os_context_t *ctx,uint8_t id)
{ (void)ctx;assert(id==16);return owns; }
esp_err_t solar_os_shell_io_printf(solar_os_shell_io_t *out,const char *fmt,...)
{
    assert(out==&io);
    if(fail_output)return ESP_FAIL;
    va_list args;va_start(args,fmt);
    /* Match the production printf's fixed buffer, including truncation. */
    char buffer[192];int n=vsnprintf(buffer,sizeof(buffer),fmt,args);va_end(args);
    assert(n>=0);size_t used=strlen(buffer);
    assert(used<sizeof(output)-output_len);memcpy(output+output_len,buffer,used+1);
    output_len+=used;return ESP_OK;
}
esp_err_t solar_os_shell_io_write_len(solar_os_shell_io_t *out,const char *text,size_t length)
{
    assert(out==&io);
    if(fail_output)return ESP_FAIL;
    assert(length<sizeof(output)-output_len);
    memcpy(output+output_len,text,length);output_len+=length;output[output_len]=0;
    return ESP_OK;
}
esp_err_t solar_os_shell_io_writeln(solar_os_shell_io_t *out,const char *text)
{ return solar_os_shell_io_printf(out,"%s\r\n",text); }
static void command(const char *verb)
{
    output_len=0;output[0]=0;
    char *argv[]={"touchraw",(char *)verb};solar_os_context_t ctx={0};
    solar_os_shell_cmd_touchraw(&ctx,2,argv);
}
int main(void)
{
    io.port=diag_owner;
    io.kind=SOLAR_OS_SHELL_IO_KIND_TERMINAL;command("start");
    assert(strstr(output,"use USB") && !raw_records);
    io.kind=SOLAR_OS_SHELL_IO_KIND_PORT;owns=false;command("start");
    assert(strstr(output,"use USB") && !raw_records);
    owns=true;valid_handle=false;command("start");assert(strstr(output,"use USB"));
    valid_handle=true;command("unknown");assert(strstr(output,"<start|status|stop|dump|clear>"));
    assert(solar_os_ft6336_attach("touch0",bindings,8)==ESP_OK);
    assert(solar_os_ft6336_set_enabled("touch0",true)==ESP_OK);
    command("start");assert(raw_status.mode==TOUCHRAW_PENDING);
    command("dump");assert(strstr(output,"stop acquisition before dump"));
    memcpy(report,(uint8_t[]){1,0x80,42,0x30,51,5,6,0x40,70,0xf0,80,11,12},13);
    poll_device(&touch);fail_i2c=true;poll_device(&touch);fail_i2c=false;
    command("stop");poll_device(&touch);command("dump");
    assert(strstr(output,"reported_sentinel=0 duplicates=0\r\n"));
    assert(strstr(output,"raw=01802a303305064046f0500b0c"));
    assert(strstr(output,"p1=3,42,51,2,1,1 p2=15,70,80,1,0,0"));
    assert(strstr(output,"measured=0 raw=00000000000000000000000000 result=0 reasons=0"));
    assert(strstr(output,"TOUCHRAW end emitted=2 stored=2 dropped=0"));
    assert(!raw_status.dumping);
    fail_output=true;command("dump");assert(!raw_status.dumping);fail_output=false;
    command("clear");assert(!raw_status.stored);
    command("dump");assert(strstr(output,"TOUCHRAW end emitted=0 stored=0 dropped=0"));
    assert(solar_os_ft6336_detach("touch0")==ESP_OK);free(raw_records);raw_records=NULL;
    puts("ft6336_raw_shell_test: 8 cases PASS");return 0;
}
