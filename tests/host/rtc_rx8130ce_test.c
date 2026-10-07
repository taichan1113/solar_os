#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "solar_os_bus_types.h"
#include "solar_os_buses.h"
#include "rtc_rx8130ce.h"
#include "solar_os_rtc.h"
#include "solar_os_expansion.h"

static uint8_t regs[256];
static int fail_reg = -1;
static bool fail_read, fail_write;
static unsigned writes, sessions, restores;
static time_t system_epoch;
size_t strlcpy(char *d, const char *s, size_t n)
{ size_t l = strlen(s); if(n) { size_t c=l<n-1?l:n-1; memcpy(d,s,c); d[c]=0; } return l; }
const char *esp_err_to_name(esp_err_t e) { (void)e; return "test-error"; }
esp_err_t solar_os_bus_i2c_read_reg(const char *b, uint8_t a, uint8_t r, uint8_t *p, size_t n)
{
    assert(!strcmp(b,"i2c0") && a==0x32);
    if(fail_read && r==fail_reg) return ESP_FAIL;
    memcpy(p,regs+r,n); return ESP_OK;
}
esp_err_t solar_os_bus_i2c_write_reg(const char *b, uint8_t a, uint8_t r, const uint8_t *p, size_t n)
{
    assert(!strcmp(b,"i2c0") && a==0x32); writes++;
    if(fail_write && r==fail_reg) return ESP_FAIL;
    if(r==0x1d) { assert(n==1 && *p==0xbd); regs[r] &= *p; }
    else memcpy(regs+r,p,n);
    return ESP_OK;
}
static esp_err_t read_op(void *c, uint8_t a, uint8_t r, uint8_t *p, size_t n)
{ (void)c; return solar_os_bus_i2c_read_reg("i2c0",a,r,p,n); }
static esp_err_t write_op(void *c, uint8_t a, uint8_t r, const uint8_t *p, size_t n)
{ (void)c; return solar_os_bus_i2c_write_reg("i2c0",a,r,p,n); }
esp_err_t solar_os_bus_i2c_transaction(const char *b,
    esp_err_t (*cb)(const solar_os_i2c_transaction_t *, void *), void *u)
{
    assert(!strcmp(b,"i2c0")); sessions++;
    const solar_os_i2c_transaction_t s={.read_reg=read_op,.write_reg=write_op};
    return cb(&s,u);
}
/* Intercept the OS syscall, while exercising the actual RTC/time services. */
int settimeofday(const struct timeval *tv, const struct timezone *tz)
{
    assert(tv && tz==NULL && tv->tv_usec==0);
    system_epoch=tv->tv_sec;
    restores++;
    return 0;
}
extern const solar_os_expansion_driver_t solar_os_rx8130ce_expansion_driver;

int main(void)
{
    rtc_rx8130ce_t rtc;
    assert(rtc_rx8130ce_init(&rtc,"i2c0",0x51)==ESP_ERR_INVALID_ARG);
    fail_read=true; fail_reg=0x1f;
    assert(rtc_rx8130ce_init(&rtc,"i2c0",0x32)==ESP_FAIL);
    fail_read=false; regs[0x1f]=0xa3; regs[0x1d]=0x9a;
    assert(rtc_rx8130ce_init(&rtc,"i2c0",0x32)==ESP_OK);
    assert(regs[0x1f]==0xb3 && regs[0x1d]==0x9a);
    solar_os_datetime_t dt={.year=2024,.month=2,.day=29,.hour=23,.minute=58,.second=59,.weekday=4};
    assert(rtc_rx8130ce_set(&rtc,&dt)==ESP_OK);
    const uint8_t expected[]={0x59,0x58,0x23,0x10,0x29,0x02,0x24};
    assert(!memcmp(regs+0x10,expected,7) && regs[0x1d]==0x98 && !(regs[0x1e]&0x40));
    solar_os_datetime_t got;
    assert(rtc_rx8130ce_get(&rtc,&got)==ESP_OK && got.year==2024 && got.day==29 && got.clock_integrity);
    dt.year=2023; unsigned before=writes;
    assert(rtc_rx8130ce_set(&rtc,&dt)==ESP_ERR_INVALID_ARG && writes==before);
    dt.year=2100; assert(rtc_rx8130ce_set(&rtc,&dt)==ESP_ERR_INVALID_ARG);
    dt.year=2024; dt.weekday=7; assert(rtc_rx8130ce_set(&rtc,&dt)==ESP_ERR_INVALID_ARG); dt.weekday=4;
    regs[0x10]=0x6a; assert(rtc_rx8130ce_get(&rtc,&got)==ESP_ERR_INVALID_STATE); regs[0x10]=0x59;
    regs[0x13]=3; assert(rtc_rx8130ce_get(&rtc,&got)==ESP_ERR_INVALID_STATE); regs[0x13]=0x10;
    regs[0x14]=0x30; assert(rtc_rx8130ce_get(&rtc,&got)==ESP_ERR_INVALID_STATE); regs[0x14]=0x29;
    regs[0x1d]|=2; assert(rtc_rx8130ce_get(&rtc,&got)==ESP_OK && !got.clock_integrity);
    regs[0x1d]&=~2; regs[0x1e]|=0x40;
    assert(rtc_rx8130ce_get(&rtc,&got)==ESP_OK && !got.clock_integrity); regs[0x1e]=0x19;
    fail_write=true; fail_reg=0x10;
    assert(rtc_rx8130ce_set(&rtc,&dt)==ESP_FAIL && regs[0x1e]==0x19);
    assert(rtc_rx8130ce_get(&rtc,&got)==ESP_OK && !got.clock_integrity);
    fail_write=false; assert(rtc_rx8130ce_set(&rtc,&dt)==ESP_OK && regs[0x1e]==0x19);
    fail_read=true; fail_reg=0x10;
    assert(rtc_rx8130ce_get(&rtc,&got)==ESP_FAIL && !got.clock_integrity); fail_read=false;
    const solar_os_expansion_binding_t b[] = {
        {.kind=SOLAR_OS_EXPANSION_BINDING_I2C_BUS,.target="i2c0"},
        {.kind=SOLAR_OS_EXPANSION_BINDING_I2C_ADDRESS,.value=0x32},
    };
    regs[0x1d]|=2;
    assert(solar_os_rx8130ce_expansion_driver.attach("rtc0",b,2)==ESP_OK && restores==0);
    solar_os_rtc_info_t info;
    assert(solar_os_rtc_get_info(&info)==ESP_OK && info.capabilities==SOLAR_OS_RTC_CAP_CALENDAR);
    assert(info.interrupt_gpio==SOLAR_OS_RTC_INTERRUPT_GPIO_NONE);
    assert(solar_os_time_set_utc_datetime(&dt)==ESP_OK && restores==1);
    assert(system_epoch==1709251139); /* 2024-02-29 23:58:59 UTC. */
    assert(solar_os_rx8130ce_expansion_driver.detach("rtc0")==ESP_OK && !solar_os_rtc_has_provider());
    assert(solar_os_rx8130ce_expansion_driver.attach("rtc0",b,2)==ESP_OK && restores==2);
    assert(solar_os_rx8130ce_expansion_driver.detach("rtc0")==ESP_OK);
    printf("rtc_rx8130ce_test: PASS (%u serialized sessions)\n",sessions);
}
