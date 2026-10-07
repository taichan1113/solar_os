#pragma once
#include "esp_err.h"
#include <stdint.h>
typedef struct { const char *server; } esp_sntp_config_t;
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(name) ((esp_sntp_config_t){.server=(name)})
void esp_netif_sntp_deinit(void);
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *);
esp_err_t esp_netif_sntp_sync_wait(uint32_t);
