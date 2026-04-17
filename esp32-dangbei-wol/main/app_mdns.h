#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_MDNS_SERVICE_TYPE "_dangbei-wol"
#define APP_MDNS_PROTOCOL     "_tcp"
#define APP_MDNS_PORT         80

esp_err_t app_mdns_start(void);

#ifdef __cplusplus
}
#endif
