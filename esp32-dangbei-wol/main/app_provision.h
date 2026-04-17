#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 在已启动的 AP 模式下挂载配网页面 + 处理 /save。
 * httpd 由调用者持有；此函数只注册 URI handler。 */
esp_err_t app_provision_register(httpd_handle_t server);

#ifdef __cplusplus
}
#endif
