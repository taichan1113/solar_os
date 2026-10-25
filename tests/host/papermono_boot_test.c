#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../src/services/solar_os_board_boot.c"
static bool enabled=true;
static unsigned usb_calls,wifi_starts,connects;
static esp_err_t usb_result, wifi_result, connect_result;
const char *esp_err_to_name(esp_err_t e){(void)e;return "fixture";}
esp_err_t solar_os_port_shell_start_with_options(solar_os_context_t *ctx,const char *port,const solar_os_port_shell_options_t *o,bool startup,uint8_t *id){assert(ctx && !strcmp(port,"cdc0") && o->cols==80 && o->rows==25 && o->terminal_profile==SOLAR_OS_SHELL_TERMINAL_PROFILE_DUMB && !startup);usb_calls++;*id=16;return usb_result;}
bool solar_os_wifi_enabled_for_current_boot(void){return enabled;}
esp_err_t solar_os_wifi_start(void){wifi_starts++;return wifi_result;}
esp_err_t solar_os_wifi_connect_saved(void){connects++;return connect_result;}
int main(void){solar_os_context_t ctx={0};
 assert(solar_os_board_boot_start_jobs(&ctx)==ESP_OK&&usb_calls==1&&wifi_starts==1&&connects==1);
 enabled=false;assert(solar_os_board_boot_start_jobs(&ctx)==ESP_OK&&usb_calls==2&&wifi_starts==1&&connects==1);
 enabled=true;connect_result=ESP_ERR_NOT_FOUND;assert(solar_os_board_boot_start_jobs(&ctx)==ESP_OK&&connects==2);
 wifi_result=ESP_FAIL;assert(solar_os_board_boot_start_jobs(&ctx)==ESP_OK&&connects==2);
 usb_result=ESP_FAIL;assert(solar_os_board_boot_start_jobs(&ctx)==ESP_FAIL);
 puts("papermono_boot_test: USB without startup, saved OFF respected, absent STA nonfatal; no NTP/AP calls PASS");}
