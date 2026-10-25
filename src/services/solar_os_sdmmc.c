#include "solar_os_sdmmc.h"

#include <stdbool.h>
#include <string.h>

#include "sd_card.h"
#include "solar_os_board_caps.h"
#include "solar_os_gpio_controller.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef struct {
    bool active, have_power_line, have_detect, powered, suspended;
    int active_level;
    solar_os_gpio_line_ref_t power_line, detect_line;
    char name[SOLAR_OS_EXPANSION_DEVICE_NAME_MAX];
} solar_os_sdmmc_device_t;

static solar_os_sdmmc_device_t sdmmc;

static esp_err_t parse_bindings(const solar_os_expansion_binding_t *bindings,
                                size_t binding_count,
                                int pins[6], int *power, int *active)
{
    static const char *const roles[] = {"clk", "cmd", "d0", "d1", "d2", "d3"};
    bool present[6] = {false};
    bool have_power = false, have_active = false;
    if (bindings == NULL || pins == NULL || power == NULL || active == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *power = -1;
    *active = 1;
    for (size_t i = 0; i < 6; i++) {
        pins[i] = -1;
    }

    for (size_t i = 0; i < binding_count; i++) {
        if (bindings[i].kind == SOLAR_OS_EXPANSION_BINDING_GPIO_LINE &&
            (!strcmp(bindings[i].role,"power_line") || !strcmp(bindings[i].role,"detect"))) {
            const bool power_role=!strcmp(bindings[i].role,"power_line");
            if (bindings[i].value<0 || bindings[i].value>255 ||
                (power_role && (have_power || sdmmc.have_power_line)) ||
                (!power_role && sdmmc.have_detect)) return ESP_ERR_INVALID_ARG;
            solar_os_gpio_line_ref_t *line=power_role ? &sdmmc.power_line : &sdmmc.detect_line;
            strlcpy(line->controller,bindings[i].target,sizeof(line->controller));
            line->line=(uint8_t)bindings[i].value;
            if (power_role) sdmmc.have_power_line=true; else sdmmc.have_detect=true;
            continue;
        }
        if (bindings[i].kind == SOLAR_OS_EXPANSION_BINDING_GPIO && !have_power && !sdmmc.have_power_line &&
            strcmp(bindings[i].role, "power") == 0) {
            *power = bindings[i].value;
            have_power = true;
            continue;
        }
        if (bindings[i].kind == SOLAR_OS_EXPANSION_BINDING_PARAMETER && !have_active &&
            strcmp(bindings[i].role, "active") == 0 &&
            bindings[i].value >= 0 && bindings[i].value <= 1) {
            *active = bindings[i].value;
            have_active = true;
            continue;
        }
        if (bindings[i].kind != SOLAR_OS_EXPANSION_BINDING_GPIO) {
            return ESP_ERR_INVALID_ARG;
        }
        size_t role = 0;
        while (role < 6 && strcmp(bindings[i].role, roles[role]) != 0) {
            role++;
        }
        if (role == 6 || present[role]) {
            return ESP_ERR_INVALID_ARG;
        }
        pins[role] = bindings[i].value;
        present[role] = true;
    }

    if ((!have_power && !sdmmc.have_power_line && have_active) || !present[0] || !present[1] || !present[2] ||
        (present[3] != present[4]) || (present[3] != present[5])) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t media_power_control(void *context, bool on)
{
    solar_os_sdmmc_device_t *s=context;
    if (!s->have_power_line || s->powered==on) return ESP_OK;
    const esp_err_t err=solar_os_gpio_line_write(&s->power_line,on ? s->active_level : !s->active_level);
    if (err==ESP_OK) { s->powered=on; if(on) vTaskDelay(pdMS_TO_TICKS(300)); }
    return err;
}
static esp_err_t media_card_present(void *context, bool *present)
{
    solar_os_sdmmc_device_t *s=context; bool high=true;
    const esp_err_t err=solar_os_gpio_line_read(&s->detect_line,&high);
    if (err==ESP_OK) *present=!high;
    return err;
}
esp_err_t solar_os_sdmmc_set_suspended(const char *name, bool suspended)
{
    if (!name || !sdmmc.active || strcmp(name,sdmmc.name)) return ESP_ERR_NOT_FOUND;
    const esp_err_t err=sd_card_set_suspended(suspended);
    if (err==ESP_OK) sdmmc.suspended=suspended;
    return err;
}
bool solar_os_sdmmc_is_ready(const char *name)
{ return name && sdmmc.active && !sdmmc.suspended && !strcmp(name,sdmmc.name) && sd_card_is_mounted(); }

esp_err_t solar_os_sdmmc_attach(const char *name,
                                const solar_os_expansion_binding_t *bindings,
                                size_t binding_count)
{
    int pins[6];
    int power, active;
    if (name == NULL || name[0] == '\0' || sdmmc.active) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(&sdmmc,0,sizeof(sdmmc));
    esp_err_t ret = parse_bindings(bindings, binding_count, pins, &power, &active);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = sd_card_configure_sdmmc(pins[0], pins[1], pins[2], pins[3], pins[4], pins[5]);
    if (ret != ESP_OK) {
        return ret;
    }

    if (power >= 0) {
        ret = sd_card_configure_sdmmc_power(power, active);
        if (ret != ESP_OK) {
            (void)sd_card_clear_sdmmc_config();
            return ret;
        }
    }

    sdmmc.active_level=active;
    if (sdmmc.have_power_line) {
        ret=solar_os_gpio_line_write(&sdmmc.power_line,!active);
        if(ret==ESP_OK) ret=solar_os_gpio_line_configure(&sdmmc.power_line,SOLAR_OS_GPIO_LINE_MODE_OUTPUT,SOLAR_OS_GPIO_LINE_PULL_NONE);
    }
    if (ret==ESP_OK && sdmmc.have_detect)
        ret=solar_os_gpio_line_configure(&sdmmc.detect_line,SOLAR_OS_GPIO_LINE_MODE_INPUT,SOLAR_OS_GPIO_LINE_PULL_UP);
    if (ret==ESP_OK && (sdmmc.have_power_line || sdmmc.have_detect))
        ret=sd_card_configure_sdmmc_media(sdmmc.have_power_line ? media_power_control : NULL,
            sdmmc.have_detect ? media_card_present : NULL,&sdmmc);
    if (ret!=ESP_OK) { (void)sd_card_clear_sdmmc_config(); return ret; }
    /* Built-in cards mount during normal storage initialization. */
    if (!solar_os_board_has(SOLAR_OS_BOARD_CAP_SD)) {
        ret = sd_card_init();
        if (ret != ESP_OK) {
            (void)sd_card_unmount();
            (void)sd_card_clear_sdmmc_config();
            return ret;
        }
    }

    sdmmc.active = true;
    strlcpy(sdmmc.name, name, sizeof(sdmmc.name));
    return ESP_OK;
}

esp_err_t solar_os_sdmmc_detach(const char *name)
{
    if (!sdmmc.active || name == NULL || strcmp(sdmmc.name, name) != 0) {
        return ESP_ERR_NOT_FOUND;
    }
    if (sd_card_has_mounts()) {
        return ESP_ERR_INVALID_STATE;
    }

    (void)sd_card_unmount();
    const esp_err_t ret = sd_card_clear_sdmmc_config();
    if (ret == ESP_OK) {
        memset(&sdmmc, 0, sizeof(sdmmc));
    }
    return ret;
}
