#include "app_wifi.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "wifi";

#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASS "pass"
#define NVS_KEY_TOKEN "token"

#define BIT_CONNECTED   BIT0
#define BIT_FAIL        BIT1

static EventGroupHandle_t s_event_group;
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static app_wifi_mode_t s_mode = APP_WIFI_MODE_NONE;
static int s_retry_count;
static bool s_sta_should_connect;

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    if (base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            if (s_sta_should_connect) {
                esp_wifi_connect();
            }
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            if (!s_sta_should_connect) {
                break;
            }
            if (s_retry_count < 3) {
                s_retry_count++;
                ESP_LOGW(TAG, "STA disconnected, retry %d/3", s_retry_count);
                esp_wifi_connect();
            } else {
                xEventGroupSetBits(s_event_group, BIT_FAIL);
            }
            break;
        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t *ev = event_data;
            ESP_LOGI(TAG, "AP client joined " MACSTR ", aid=%d",
                     MAC2STR(ev->mac), ev->aid);
            break;
        }
        case WIFI_EVENT_AP_STADISCONNECTED: {
            wifi_event_ap_stadisconnected_t *ev = event_data;
            ESP_LOGI(TAG, "AP client left " MACSTR ", aid=%d",
                     MAC2STR(ev->mac), ev->aid);
            break;
        }
        default:
            break;
        }
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = event_data;
        ESP_LOGI(TAG, "STA got IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        s_retry_count = 0;
        xEventGroupSetBits(s_event_group, BIT_CONNECTED);
    }
}

esp_err_t app_wifi_init(void)
{
    s_event_group = xEventGroupCreate();
    if (s_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_netif_init());

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    return ESP_OK;
}

static esp_err_t load_credentials(char *ssid, size_t ssid_len,
                                  char *pass, size_t pass_len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(APP_WIFI_NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }
    size_t len_s = ssid_len;
    err = nvs_get_str(h, NVS_KEY_SSID, ssid, &len_s);
    if (err != ESP_OK) {
        nvs_close(h);
        return err;
    }
    size_t len_p = pass_len;
    err = nvs_get_str(h, NVS_KEY_PASS, pass, &len_p);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        pass[0] = '\0';
        err = ESP_OK;
    }
    nvs_close(h);
    return err;
}

bool app_wifi_has_credentials(void)
{
    char ssid[APP_WIFI_MAX_SSID_LEN + 1] = {0};
    char pass[APP_WIFI_MAX_PASS_LEN + 1] = {0};
    return load_credentials(ssid, sizeof(ssid), pass, sizeof(pass)) == ESP_OK;
}

esp_err_t app_wifi_start_sta(uint32_t timeout_ms)
{
    char ssid[APP_WIFI_MAX_SSID_LEN + 1] = {0};
    char pass[APP_WIFI_MAX_PASS_LEN + 1] = {0};
    esp_err_t err = load_credentials(ssid, sizeof(ssid), pass, sizeof(pass));
    if (err != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "starting STA, SSID=%s", ssid);
    s_sta_should_connect = true;
    s_retry_count = 0;
    xEventGroupClearBits(s_event_group, BIT_CONNECTED | BIT_FAIL);

    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, pass, sizeof(wifi_cfg.sta.password) - 1);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    wifi_cfg.sta.pmf_cfg.capable = true;

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_mode = APP_WIFI_MODE_STA;

    EventBits_t bits = xEventGroupWaitBits(
        s_event_group, BIT_CONNECTED | BIT_FAIL, pdFALSE, pdFALSE,
        pdMS_TO_TICKS(timeout_ms));

    if (bits & BIT_CONNECTED) {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "STA connect failed/timeout");
    esp_wifi_stop();
    s_sta_should_connect = false;
    s_mode = APP_WIFI_MODE_NONE;
    return ESP_ERR_TIMEOUT;
}

esp_err_t app_wifi_start_ap(void)
{
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP));

    wifi_config_t wifi_cfg = {0};
    snprintf((char *)wifi_cfg.ap.ssid, sizeof(wifi_cfg.ap.ssid),
             "%s%02X%02X", APP_WIFI_AP_PREFIX, mac[4], mac[5]);
    wifi_cfg.ap.ssid_len = strlen((char *)wifi_cfg.ap.ssid);
    wifi_cfg.ap.channel = 6;
    wifi_cfg.ap.max_connection = 4;
    wifi_cfg.ap.authmode = WIFI_AUTH_OPEN;

    s_sta_should_connect = false;
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_mode = APP_WIFI_MODE_AP;
    ESP_LOGI(TAG, "SoftAP started, SSID=%s", wifi_cfg.ap.ssid);
    return ESP_OK;
}

esp_err_t app_wifi_save_credentials(
    const char *ssid, const char *password, const char *token
)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(APP_WIFI_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, NVS_KEY_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(h, NVS_KEY_PASS, password ? password : "");
    }
    if (err == ESP_OK) {
        err = nvs_set_str(h, NVS_KEY_TOKEN, token ? token : "");
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t app_wifi_clear_credentials(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(APP_WIFI_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    nvs_erase_key(h, NVS_KEY_SSID);
    nvs_erase_key(h, NVS_KEY_PASS);
    nvs_erase_key(h, NVS_KEY_TOKEN);
    err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t app_wifi_load_token(char *token, size_t token_len)
{
    if (token == NULL || token_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    token[0] = '\0';

    nvs_handle_t h;
    esp_err_t err = nvs_open(APP_WIFI_NVS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    size_t len = token_len;
    err = nvs_get_str(h, NVS_KEY_TOKEN, token, &len);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        token[0] = '\0';
        return ESP_OK;
    }
    return err;
}

static int compare_scan_result(const void *lhs, const void *rhs)
{
    const wifi_ap_record_t *a = lhs;
    const wifi_ap_record_t *b = rhs;
    return b->rssi - a->rssi;
}

esp_err_t app_wifi_scan_networks(
    app_wifi_scan_result_t *results, size_t max_results, size_t *out_count
)
{
    if (results == NULL || max_results == 0 || out_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_count = 0;

    wifi_scan_config_t scan_cfg = {
        .show_hidden = false,
    };
    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) {
        return err;
    }

    uint16_t ap_count = 0;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    if (ap_count == 0) {
        return ESP_OK;
    }

    wifi_ap_record_t *records = calloc(ap_count, sizeof(wifi_ap_record_t));
    if (records == NULL) {
        esp_wifi_clear_ap_list();
        return ESP_ERR_NO_MEM;
    }

    err = esp_wifi_scan_get_ap_records(&ap_count, records);
    if (err != ESP_OK) {
        free(records);
        return err;
    }

    qsort(records, ap_count, sizeof(wifi_ap_record_t), compare_scan_result);

    size_t written = 0;
    for (uint16_t idx = 0; idx < ap_count && written < max_results; idx++) {
        if (records[idx].ssid[0] == '\0') {
            continue;
        }
        strncpy(
            results[written].ssid,
            (const char *)records[idx].ssid,
            sizeof(results[written].ssid) - 1
        );
        results[written].ssid[sizeof(results[written].ssid) - 1] = '\0';
        results[written].rssi = records[idx].rssi;
        results[written].authmode = records[idx].authmode;
        written++;
    }

    free(records);
    *out_count = written;
    return ESP_OK;
}

app_wifi_mode_t app_wifi_current_mode(void)
{
    return s_mode;
}

esp_netif_t *app_wifi_sta_netif(void)
{
    return s_sta_netif;
}

esp_netif_t *app_wifi_ap_netif(void)
{
    return s_ap_netif;
}
