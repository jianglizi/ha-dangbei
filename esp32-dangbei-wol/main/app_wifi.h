#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_netif.h"
#include "esp_wifi_types_generic.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_WIFI_NVS_NAMESPACE "wifi"
#define APP_WIFI_AP_PREFIX     "DangbeiWOL-"
#define APP_WIFI_STA_TIMEOUT_MS 15000
#define APP_WIFI_MAX_SSID_LEN 32
#define APP_WIFI_MAX_PASS_LEN 64
#define APP_WIFI_MAX_TOKEN_LEN 64

typedef struct {
    char ssid[APP_WIFI_MAX_SSID_LEN + 1];
    int8_t rssi;
    wifi_auth_mode_t authmode;
} app_wifi_scan_result_t;

typedef enum {
    APP_WIFI_MODE_NONE = 0,
    APP_WIFI_MODE_STA,
    APP_WIFI_MODE_AP,
} app_wifi_mode_t;

esp_err_t app_wifi_init(void);

/* 读 NVS：若有凭据则启动 STA 并等待连接；返回 ESP_OK 表示已连上。
 * timeout_ms 内若无 IP，返回 ESP_ERR_TIMEOUT；无凭据返回 ESP_ERR_NOT_FOUND。 */
esp_err_t app_wifi_start_sta(uint32_t timeout_ms);

/* 启动 SoftAP，SSID = APP_WIFI_AP_PREFIX + MAC 后 4 hex，密码空（开放）。 */
esp_err_t app_wifi_start_ap(void);

/* 把凭据写入 NVS。 */
esp_err_t app_wifi_save_credentials(
    const char *ssid, const char *password, const char *token
);

/* 删除 NVS 凭据。 */
esp_err_t app_wifi_clear_credentials(void);

bool app_wifi_has_credentials(void);

/* 读取 Bearer Token；若不存在则返回空字符串。 */
esp_err_t app_wifi_load_token(char *token, size_t token_len);

/* 扫描附近 WiFi；结果按 RSSI 降序。 */
esp_err_t app_wifi_scan_networks(
    app_wifi_scan_result_t *results, size_t max_results, size_t *out_count
);

app_wifi_mode_t app_wifi_current_mode(void);

esp_netif_t *app_wifi_sta_netif(void);
esp_netif_t *app_wifi_ap_netif(void);

#ifdef __cplusplus
}
#endif
