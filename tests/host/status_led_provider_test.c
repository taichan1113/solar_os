#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "solar_os_status_led.h"

static bool hardware_on;
static bool fail;
static esp_err_t set(void *user,bool on)
{ assert(user==&hardware_on); if(fail)return ESP_FAIL; hardware_on=on;return ESP_OK; }
static esp_err_t get(void *user,bool *on)
{ assert(user==&hardware_on); if(fail)return ESP_FAIL;*on=hardware_on;return ESP_OK; }
int main(void)
{
    bool on;
    assert(solar_os_status_led_init()==ESP_ERR_NOT_SUPPORTED);
    const solar_os_status_led_provider_t p={.set=set,.get=get,.user=&hardware_on};
    assert(solar_os_status_led_register_provider("pm1",&p)==ESP_OK);
    assert(solar_os_status_led_register_provider("other",&p)==ESP_ERR_INVALID_STATE);
    hardware_on=true;
    assert(solar_os_status_led_init()==ESP_OK && !hardware_on);
    assert(solar_os_status_led_toggle(&on)==ESP_OK && on && hardware_on);
    assert(solar_os_status_led_get(&on)==ESP_OK && on);
    fail=true;
    assert(solar_os_status_led_set(false)==ESP_FAIL && hardware_on);
    assert(solar_os_status_led_toggle(&on)==ESP_FAIL && hardware_on);
    fail=false;
    assert(solar_os_status_led_set(false)==ESP_OK && !hardware_on);
    assert(solar_os_status_led_unregister_provider("other")==ESP_ERR_NOT_FOUND);
    assert(solar_os_status_led_unregister_provider("pm1")==ESP_OK);
    assert(solar_os_status_led_set(true)==ESP_ERR_NOT_SUPPORTED);
    puts("status_led_provider_test: PASS");
}
