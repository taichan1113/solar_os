#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "esp_err.h"
#define SD_OCR_SDHC_CAP (1U<<30)
#define SD_OCR_S18_RA (1U<<24)
#define SDMMC_DISCARD_ARG 1
#define SDMMC_ERASE_ARG 0
typedef int sdmmc_erase_arg_t;
typedef struct { int flags,slot; esp_err_t (*init)(void),(*deinit)(void),(*deinit_p)(int); } sdmmc_host_t;
typedef struct {
 bool is_sdio,is_mmc,is_ddr;
 unsigned ocr,log_bus_width,real_freq_khz,max_freq_khz,epoch;
 struct { char name[8]; } cid;
 struct { unsigned capacity,sector_size,csd_ver,read_block_len; } csd;
 struct { unsigned cur_bus_width; } ssr;
} sdmmc_card_t;
esp_err_t sdmmc_card_init(const sdmmc_host_t *,sdmmc_card_t *);
esp_err_t sdmmc_get_status(sdmmc_card_t *);
esp_err_t sdmmc_read_sectors(sdmmc_card_t *,void *,size_t,size_t);
esp_err_t sdmmc_write_sectors(sdmmc_card_t *,const void *,size_t,size_t);
esp_err_t sdmmc_can_trim(sdmmc_card_t *);
esp_err_t sdmmc_can_discard(sdmmc_card_t *);
esp_err_t sdmmc_erase_sectors(sdmmc_card_t *,size_t,size_t,sdmmc_erase_arg_t);
void sdmmc_card_print_info(FILE *,const sdmmc_card_t *);
