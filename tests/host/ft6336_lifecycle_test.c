#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "driver/gpio.h"
#include "../../src/services/solar_os_ft6336.c"

static bool input_ready, power_high, reset_high, irq_off;
static bool fail_i2c, fail_reset, fail_task, fail_stop, fail_pointer;
static bool fail_alloc, fail_quiesce, usb_connected = true;
static bool valid_handle = true;
static const solar_os_port_handle_t diag_owner = {.index=0,.token=1};
static int64_t fake_us;
static unsigned read6, read13;
static unsigned i2c_reads, tasks, stops, releases, pointer_writes;
static solar_os_input_pointer_event_t last_pointer;
static uint8_t report[13];
static char sequence[128];
static size_t seq_len;
static void record(char c) { assert(seq_len+1<sizeof(sequence)); sequence[seq_len++]=c; sequence[seq_len]=0; }
size_t strlcpy(char *d,const char *s,size_t n)
{ size_t l=strlen(s); if(n) {size_t c=l<n-1?l:n-1; memcpy(d,s,c);d[c]=0;}return l; }
const char *esp_err_to_name(esp_err_t e) { (void)e; return "error"; }
int64_t esp_timer_get_time(void) { return fake_us++; }
void *solar_os_memory_alloc(size_t n,solar_os_memory_class_t c,const char *tag)
{ assert(c==SOLAR_OS_MEMORY_EXTERNAL_REQUIRED && !strcmp(tag,"touchraw"));return fail_alloc?NULL:malloc(n); }
void solar_os_cdc_get_status(solar_os_cdc_status_t *s)
{ *s=(solar_os_cdc_status_t){.connected=usb_connected,.port_claimed=true};strcpy(s->port_owner,"session:16"); }
bool solar_os_port_handle_valid(const solar_os_port_handle_t *p)
{ return p && valid_handle && p->index==diag_owner.index && p->token==diag_owner.token; }
void vTaskDelay(TickType_t ticks) { assert(ticks>0); }
unsigned ulTaskNotifyTake(int clear, TickType_t ticks) { (void)clear;(void)ticks; return 0; }
void xTaskNotifyGive(TaskHandle_t h) { assert(h!=NULL); }
void solar_os_task_delete_internal(TaskHandle_t h) { (void)h; }
BaseType_t solar_os_task_create_pinned_internal(TaskFunction_t f,const char *n,uint32_t stack,
    void *arg,UBaseType_t prio,TaskHandle_t *h,BaseType_t core,int role)
{
    (void)n;(void)stack;(void)prio;(void)core;(void)role;
    assert(f==ft6336_worker && arg==&touch && power_high && reset_high);
    assert(input_ready || raw_status.mode==TOUCHRAW_WAIT_RELEASE);
    if(fail_task)return 0;
    *h=(TaskHandle_t)&tasks; tasks++; record('T'); return pdPASS;
}
bool solar_os_task_wait_done(TaskHandle_t h,volatile bool *done,uint32_t ms)
{
    assert(h && ms && touch.stop_requested && !input_ready); stops++; record('S');
    if(fail_stop)return false;
    *done=true; return true;
}
esp_err_t gpio_config(const gpio_config_t *c)
{
    assert(c->pin_bit_mask==(1ULL<<4));
    irq_off=c->mode==GPIO_MODE_DISABLE;
    if(irq_off) assert(!c->pull_up_en && !c->pull_down_en && !c->intr_type);
    return ESP_OK;
}
esp_err_t gpio_set_level(gpio_num_t p,uint32_t l) { (void)p;(void)l; return ESP_OK; }
esp_err_t solar_os_gpio_line_write(const solar_os_gpio_line_ref_t *line,bool high)
{
    assert(!strcmp(line->controller,"ioe0"));
    if(line->line==12) { power_high=high; record(high?'P':'p'); }
    else { assert(line->line==5); if(fail_reset)return ESP_FAIL; reset_high=high;record(high?'R':'r'); }
    return ESP_OK;
}
bool solar_os_display_find_target(const char *name,solar_os_display_target_t *d)
{ assert(!strcmp(name,"display0"));d->width=480;d->height=800;return true; }
esp_err_t solar_os_input_touch_source_open(const char *n,solar_os_input_source_t *s)
{ assert(!strcmp(n,"touch0")); *s=1;input_ready=true;return ESP_OK; }
void solar_os_input_source_close(solar_os_input_source_t s) { assert(s==1);input_ready=false; }
esp_err_t solar_os_input_source_set_ready(solar_os_input_source_t s,bool ready)
{ assert(s==1);input_ready=ready;record(ready?'I':'i');return ESP_OK; }
esp_err_t solar_os_input_pointer_source_quiesce(solar_os_input_source_t s)
{ assert(s==1 && !input_ready); releases++;return fail_quiesce?ESP_FAIL:ESP_OK; }
esp_err_t solar_os_input_write_pointer(solar_os_input_source_t s,const solar_os_input_pointer_event_t *e)
{ assert(s==1 && input_ready && !strcmp(e->target,"display0"));pointer_writes++;last_pointer=*e;return fail_pointer?ESP_ERR_NO_MEM:ESP_OK; }
esp_err_t solar_os_bus_i2c_read_reg(const char *bus,uint8_t addr,uint8_t reg,uint8_t *out,size_t n)
{
    assert(!strcmp(bus,"i2c0") && addr==0x38 && power_high && reset_high);i2c_reads++;record('B');
    if(fail_i2c)return ESP_FAIL;
    if(reg==0xa3) {*out=0x64;assert(n==1);} else {
        assert(reg==2 && (n==6 || n==13));
        if(n==6)read6++;else read13++;
        memcpy(out,report,n);
    }
    return ESP_OK;
}
static const solar_os_expansion_binding_t bindings[]={
    {.kind=SOLAR_OS_EXPANSION_BINDING_I2C_BUS,.target="i2c0"},
    {.kind=SOLAR_OS_EXPANSION_BINDING_I2C_ADDRESS,.value=0x38},
    {.kind=SOLAR_OS_EXPANSION_BINDING_GPIO_LINE,.target="ioe0",.role="reset",.value=5},
    {.kind=SOLAR_OS_EXPANSION_BINDING_GPIO_LINE,.target="ioe0",.role="power",.value=12},
    {.kind=SOLAR_OS_EXPANSION_BINDING_GPIO,.role="irq",.value=4},
    {.kind=SOLAR_OS_EXPANSION_BINDING_PARAMETER,.role="rotation",.value=0},
    {.kind=SOLAR_OS_EXPANSION_BINDING_PARAMETER,.role="enabled",.value=0},
    {.kind=SOLAR_OS_EXPANSION_BINDING_PARAMETER,.role="managed_power",.value=1},
};
int main(void)
{
    assert(solar_os_ft6336_attach("touch0",bindings,8)==ESP_OK);
    assert(!input_ready && !power_high && !reset_high && irq_off && !tasks && !i2c_reads);
    assert(!solar_os_ft6336_is_ready("touch0"));
    for(unsigned i=0;i<3;i++) {
        seq_len=0;sequence[0]=0;
        assert(solar_os_ft6336_set_enabled("touch0",true)==ESP_OK);
        assert(strstr(sequence,"PrRBIT") && input_ready && !irq_off);
        report[0]=1; report[1]=0;report[2]=40;report[3]=0;report[4]=50;
        poll_device(&touch); assert(touch.pressed);
        unsigned before=i2c_reads;
        assert(solar_os_ft6336_set_enabled("touch0",false)==ESP_OK);
        assert(!touch.pressed && !touch.worker_task && !input_ready && !power_high && !reset_high && irq_off);
        poll_device(&touch); assert(i2c_reads==before);
    }
    assert(stops==3 && releases>=4);
    assert(solar_os_ft6336_set_suspended("touch0",true)==ESP_OK);
    unsigned suspended_reads = i2c_reads;
    assert(solar_os_ft6336_set_enabled("touch0",true)==ESP_OK);
    assert(!input_ready && !power_high && !touch.worker_task && i2c_reads==suspended_reads);
    assert(solar_os_ft6336_set_suspended("touch0",false)==ESP_OK && input_ready && power_high);
    assert(solar_os_ft6336_set_enabled("touch0",false)==ESP_OK);
    assert(solar_os_ft6336_set_suspended("touch0",true)==ESP_OK);
    assert(solar_os_ft6336_set_suspended("touch0",false)==ESP_OK && !input_ready);
    assert(solar_os_ft6336_set_enabled("touch0",true)==ESP_OK);
    assert(solar_os_ft6336_set_suspended("touch0",true)==ESP_OK && !input_ready);
    assert(solar_os_ft6336_set_suspended("touch0",false)==ESP_OK && input_ready);
    fail_stop=true;
    assert(solar_os_ft6336_set_enabled("touch0",false)==ESP_ERR_TIMEOUT);
    unsigned before=i2c_reads;
    assert(solar_os_ft6336_set_enabled("touch0",true)==ESP_ERR_TIMEOUT && i2c_reads==before);
    fail_stop=false;assert(solar_os_ft6336_set_enabled("touch0",false)==ESP_OK);
    fail_i2c=true;
    assert(solar_os_ft6336_set_enabled("touch0",true)==ESP_FAIL);
    assert(!input_ready && !power_high && irq_off);fail_i2c=false;
    fail_reset=true;
    assert(solar_os_ft6336_set_enabled("touch0",true)==ESP_FAIL && !power_high);fail_reset=false;
    fail_task=true;
    assert(solar_os_ft6336_set_enabled("touch0",true)==ESP_ERR_NO_MEM && !power_high && !input_ready);fail_task=false;
    assert(solar_os_ft6336_set_enabled("touch0",true)==ESP_OK);
    /* Boundary coordinate, duplicate sample, rapid MOVE sequence and failed
     * RELEASE retry all exercise the real service's action state machine. */
    report[0]=1;report[1]=1;report[2]=0xdf;report[3]=3;report[4]=0x1f;
    unsigned writes_before=pointer_writes;
    poll_device(&touch);
    assert(pointer_writes==writes_before+1 && last_pointer.action==SOLAR_OS_INPUT_POINTER_PRESS);
    assert(last_pointer.x==479 && last_pointer.y==799);
    poll_device(&touch);assert(pointer_writes==writes_before+1);
    for(unsigned x=478;x>=458;x--) {
        report[1]=(uint8_t)(x>>8);report[2]=(uint8_t)x;
        poll_device(&touch);
        assert(last_pointer.action==SOLAR_OS_INPUT_POINTER_MOVE && last_pointer.x==(int16_t)x);
    }
    assert(pointer_writes==writes_before+22);
    report[0]=0;fail_pointer=true;poll_device(&touch);
    assert(touch.pressed && last_pointer.action==SOLAR_OS_INPUT_POINTER_RELEASE);
    fail_pointer=false;poll_device(&touch);assert(!touch.pressed);
    assert(solar_os_ft6336_detach("touch0")==ESP_OK);
    solar_os_ft6336_device_t legacy={0};
    assert(parse_bindings(bindings,6,&legacy)==ESP_OK && legacy.requested && !legacy.managed_power);
    puts("ft6336_lifecycle_test: PASS");
    return 0;
}
