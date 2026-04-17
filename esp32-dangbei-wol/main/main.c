#include <stdio.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "app_ble_wakeup.h"
#include "app_captive_dns.h"
#include "app_http.h"
#include "app_mdns.h"
#include "app_provision.h"
#include "app_status.h"
#include "app_wake_profile.h"
#include "app_wifi.h"

static const char *TAG = "main";

static httpd_handle_t start_http_server(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.max_uri_handlers = 12;
    cfg.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return NULL;
    }
    return server;
}

void app_main(void)
{
    app_status_init();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(app_wake_profile_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(app_wifi_init());

    bool sta_ok = false;
    if (app_wifi_has_credentials()) {
        if (app_wifi_start_sta(APP_WIFI_STA_TIMEOUT_MS) == ESP_OK) {
            sta_ok = true;
        } else {
            ESP_LOGW(TAG, "STA failed, falling back to AP provisioning");
        }
    } else {
        ESP_LOGI(TAG, "no credentials, starting AP provisioning");
    }

    httpd_handle_t server = start_http_server();
    if (server == NULL) {
        ESP_LOGE(TAG, "cannot start http server");
        return;
    }

    if (sta_ok) {
        ESP_ERROR_CHECK(app_mdns_start());
        ESP_ERROR_CHECK(app_http_register(server));
        ESP_ERROR_CHECK(app_ble_wakeup_init());
        ESP_LOGI(TAG, "running in STA mode, REST API ready on port 80");
    } else {
        ESP_ERROR_CHECK(app_wifi_start_ap());
        ESP_ERROR_CHECK(app_provision_register(server));
        ESP_ERROR_CHECK(app_captive_dns_start());
        ESP_LOGI(TAG, "running in AP mode, provisioning at http://192.168.4.1/");
    }
}
