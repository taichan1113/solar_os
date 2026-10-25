#include <assert.h>
#include <stdio.h>
#include "../../src/services/solar_os_sdmmc.c"
#include "../../src/services/solar_os_sdmmc_driver.c"

static int configured, power_configured, cleared, mounted;
static bool built_in = true, has_mounts, mono;
static sd_card_media_power_fn power_fn;
static sd_card_media_detect_fn detect_fn;
static void *media_ctx;
static bool suspended, power_on, card_present;
static esp_err_t ioe_error;
static unsigned power_writes;
static esp_err_t power_error, mount_error;
size_t strlcpy(char *dst, const char *src, size_t size)
{ size_t len=strlen(src); if (size) { size_t n=len<size-1?len:size-1; memcpy(dst,src,n); dst[n]=0; } return len; }
bool solar_os_board_has(solar_os_board_capabilities_t cap)
{ assert(cap==SOLAR_OS_BOARD_CAP_SD); return built_in; }
esp_err_t sd_card_configure_sdmmc(int clk, int cmd, int d0, int d1, int d2, int d3)
{ if (mono) assert(clk==13 && cmd==12 && d0==11 && d1==10 && d2==9 && d3==8);
  else { assert(clk==41 && cmd==42 && d0==40 && d1==-1 && d2==-1 && d3==-1); }
  configured++; return ESP_OK; }
esp_err_t sd_card_configure_sdmmc_power(int pin, int active)
{ assert(pin==5 && active==0 && configured); power_configured++; return power_error; }
esp_err_t sd_card_clear_sdmmc_config(void) { cleared++; return ESP_OK; }
esp_err_t sd_card_init(void) { mounted++; return mount_error; }
esp_err_t sd_card_unmount(void) { return ESP_OK; }
bool sd_card_has_mounts(void) { return has_mounts; }

esp_err_t sd_card_configure_sdmmc_media(sd_card_media_power_fn p, sd_card_media_detect_fn d, void *c)
{ power_fn=p; detect_fn=d; media_ctx=c; return ESP_OK; }
esp_err_t sd_card_set_suspended(bool s) { suspended=s; return ESP_OK; }
bool sd_card_is_mounted(void) { return has_mounts; }
void vTaskDelay(TickType_t ticks) { assert(ticks==300); }
esp_err_t solar_os_gpio_line_configure(const solar_os_gpio_line_ref_t *r, solar_os_gpio_line_mode_t m, solar_os_gpio_line_pull_t p)
{ assert(!strcmp(r->controller,"ioe0")); assert((r->line==13 && m==SOLAR_OS_GPIO_LINE_MODE_OUTPUT && p==SOLAR_OS_GPIO_LINE_PULL_NONE) || (r->line==0 && m==SOLAR_OS_GPIO_LINE_MODE_INPUT && p==SOLAR_OS_GPIO_LINE_PULL_UP)); return ioe_error; }
esp_err_t solar_os_gpio_line_write(const solar_os_gpio_line_ref_t *r, bool high)
{ assert(!strcmp(r->controller,"ioe0") && r->line==13); if(ioe_error) return ioe_error; power_on=high; power_writes++; return ESP_OK; }
esp_err_t solar_os_gpio_line_read(const solar_os_gpio_line_ref_t *r, bool *high)
{ assert(!strcmp(r->controller,"ioe0") && r->line==0); if(ioe_error) return ioe_error; *high=!card_present; return ESP_OK; }
int main(void)
{
    solar_os_expansion_binding_t b[] = {
        {.kind=SOLAR_OS_EXPANSION_BINDING_GPIO, .role="clk", .value=41},
        {.kind=SOLAR_OS_EXPANSION_BINDING_GPIO, .role="cmd", .value=42},
        {.kind=SOLAR_OS_EXPANSION_BINDING_GPIO, .role="d0", .value=40},
        {.kind=SOLAR_OS_EXPANSION_BINDING_GPIO, .role="power", .value=5},
        {.kind=SOLAR_OS_EXPANSION_BINDING_PARAMETER, .role="active", .value=0},
    };
    assert(solar_os_sdmmc_attach("storage0",b,5)==ESP_OK && power_configured==1 && mounted==0);
    has_mounts=true; assert(solar_os_sdmmc_detach("storage0")==ESP_ERR_INVALID_STATE);
    has_mounts=false; assert(solar_os_sdmmc_detach("storage0")==ESP_OK && cleared==1);
    power_error=ESP_FAIL;
    assert(solar_os_sdmmc_attach("storage0",b,5)==ESP_FAIL && cleared==2 && !sdmmc.active);
    power_error=ESP_OK; built_in=false; mount_error=ESP_ERR_TIMEOUT;
    assert(solar_os_sdmmc_attach("storage0",b,5)==ESP_ERR_TIMEOUT && cleared==3 && mounted==1);
    mount_error=ESP_OK;
    assert(solar_os_sdmmc_attach("storage0",b,3)==ESP_OK && power_configured==3 && mounted==2);
    assert(solar_os_sdmmc_detach("storage0")==ESP_OK);
    b[3]=b[4];
    assert(solar_os_sdmmc_attach("storage0",b,4)==ESP_ERR_INVALID_ARG);
    mono=true; built_in=true;
    solar_os_expansion_binding_t mb[]={
      {.kind=SOLAR_OS_EXPANSION_BINDING_GPIO,.role="clk",.value=13},
      {.kind=SOLAR_OS_EXPANSION_BINDING_GPIO,.role="cmd",.value=12},
      {.kind=SOLAR_OS_EXPANSION_BINDING_GPIO,.role="d0",.value=11},
      {.kind=SOLAR_OS_EXPANSION_BINDING_GPIO,.role="d1",.value=10},
      {.kind=SOLAR_OS_EXPANSION_BINDING_GPIO,.role="d2",.value=9},
      {.kind=SOLAR_OS_EXPANSION_BINDING_GPIO,.role="d3",.value=8},
      {.kind=SOLAR_OS_EXPANSION_BINDING_GPIO_LINE,.role="power_line",.target="ioe0",.value=13},
      {.kind=SOLAR_OS_EXPANSION_BINDING_GPIO_LINE,.role="detect",.target="ioe0",.value=0},
      {.kind=SOLAR_OS_EXPANSION_BINDING_PARAMETER,.role="active",.value=1}};
    int before=mounted;
    assert(solar_os_sdmmc_attach("storage0",mb,9)==ESP_OK && mounted==before && !power_on);
    assert(power_fn && detect_fn && power_fn(media_ctx,true)==ESP_OK && power_on);
    unsigned pw=power_writes;
    assert(power_fn(media_ctx,true)==ESP_OK && power_writes==pw);
    bool present=true;
    assert(detect_fn(media_ctx,&present)==ESP_OK && !present); /* empty slot is not an attach failure */
    card_present=true; assert(detect_fn(media_ctx,&present)==ESP_OK && present);
    ioe_error=ESP_ERR_TIMEOUT; assert(detect_fn(media_ctx,&present)==ESP_ERR_TIMEOUT);
    ioe_error=ESP_OK; has_mounts=true;
    assert(solar_os_sdmmc_is_ready("storage0"));
    assert(solar_os_sdmmc_set_suspended("storage0",true)==ESP_OK && suspended && !solar_os_sdmmc_is_ready("storage0") && power_on);
    assert(solar_os_sdmmc_set_suspended("storage0",false)==ESP_OK && !suspended && solar_os_sdmmc_is_ready("storage0") && power_writes==pw);
    card_present=false; assert(detect_fn(media_ctx,&present)==ESP_OK && !present);
    card_present=true; assert(detect_fn(media_ctx,&present)==ESP_OK && present);
    has_mounts=false; assert(solar_os_sdmmc_detach("storage0")==ESP_OK);
    ioe_error=ESP_FAIL; assert(solar_os_sdmmc_attach("storage0",mb,9)==ESP_FAIL && !sdmmc.active);
    puts("SDMMC: legacy 1-bit/native power, PaperMono 4-bit/IOE, detect/error, retained-power sleep/wake PASS");
}
