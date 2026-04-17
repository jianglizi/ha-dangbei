#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 在已启动的 STA 模式下挂载 REST API。 */
esp_err_t app_http_register(httpd_handle_t server);

#ifdef __cplusplus
}
#endif
