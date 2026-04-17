#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 启动一个最小 DNS server，把所有 A 记录解析到 SoftAP IP。 */
esp_err_t app_captive_dns_start(void);

#ifdef __cplusplus
}
#endif
