#pragma once
#include "sdmmc_cmd.h"
#define SDMMC_HOST_FLAG_DEINIT_ARG 1
#define SDMMC_SLOT_FLAG_INTERNAL_PULLUP 1
typedef struct { int width,flags,clk,cmd,d0,d1,d2,d3; } sdmmc_slot_config_t;
esp_err_t fixture_host_init(void);
esp_err_t fixture_host_deinit(void);
esp_err_t sdmmc_host_init_slot(int slot,const sdmmc_slot_config_t *config);
#define SDMMC_HOST_DEFAULT() { .slot=1,.init=fixture_host_init,.deinit=fixture_host_deinit }
#define SDMMC_SLOT_CONFIG_DEFAULT() { .width=4 }
