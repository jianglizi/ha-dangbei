#include "app_http.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "app_ble_wakeup.h"
#include "app_status.h"
#include "app_wake_profile.h"
#include "app_wifi.h"

static const char *TAG = "http";

static const char *status_text(int status_code)
{
    switch (status_code) {
    case 200:
        return "200 OK";
    case 202:
        return "202 Accepted";
    case 400:
        return "400 Bad Request";
    case 401:
        return "401 Unauthorized";
    case 409:
        return "409 Conflict";
    case 500:
        return "500 Internal Server Error";
    default:
        return "200 OK";
    }
}

static esp_err_t send_json(httpd_req_t *req, int status_code, const char *json)
{
    httpd_resp_set_status(req, status_text(status_code));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static bool ensure_authorized(httpd_req_t *req)
{
    char token[APP_WIFI_MAX_TOKEN_LEN + 1] = {0};
    esp_err_t err = app_wifi_load_token(token, sizeof(token));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to load auth token: %s", esp_err_to_name(err));
        send_json(req, 500, "{\"error\":\"token_load_failed\"}");
        return false;
    }
    if (token[0] == '\0') {
        return true;
    }

    size_t hdr_len = httpd_req_get_hdr_value_len(req, "Authorization");
    if (hdr_len == 0 || hdr_len >= 128) {
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Bearer");
        send_json(req, 401, "{\"error\":\"unauthorized\"}");
        return false;
    }

    char header[128] = {0};
    if (httpd_req_get_hdr_value_str(req, "Authorization", header, sizeof(header)) != ESP_OK) {
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Bearer");
        send_json(req, 401, "{\"error\":\"unauthorized\"}");
        return false;
    }

    const char *prefix = "Bearer ";
    if (strncmp(header, prefix, strlen(prefix)) != 0 ||
        strcmp(header + strlen(prefix), token) != 0) {
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Bearer");
        send_json(req, 401, "{\"error\":\"unauthorized\"}");
        return false;
    }

    return true;
}

static esp_err_t recv_request_body(httpd_req_t *req, char *buf, size_t buf_size)
{
    int total = 0;
    int remaining = req->content_len;

    if (buf == NULL || buf_size == 0 || remaining <= 0 || remaining >= (int)buf_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    while (remaining > 0) {
        int received = httpd_req_recv(req, buf + total, remaining);
        if (received <= 0) {
            return ESP_FAIL;
        }
        total += received;
        remaining -= received;
    }

    buf[total] = '\0';
    return ESP_OK;
}

static void load_wake_config_strings(
    char *profile,
    size_t profile_len,
    char *custom_format,
    size_t custom_format_len,
    char *custom_hex,
    size_t custom_hex_len
)
{
    app_wake_profile_config_t config;
    app_wake_profile_get_config(&config);

    snprintf(profile, profile_len, "%s",
             app_wake_profile_profile_name(config.profile));
    snprintf(custom_format, custom_format_len, "%s",
             app_wake_profile_custom_format_name(config.custom_format));
    snprintf(custom_hex, custom_hex_len, "%s", config.custom_hex);
}

static esp_err_t send_wake_config_json(httpd_req_t *req, int status_code)
{
    char profile[24];
    char custom_format[24];
    char custom_hex[APP_WAKE_PROFILE_MAX_CUSTOM_HEX_LEN + 1];
    load_wake_config_strings(profile, sizeof(profile),
                             custom_format, sizeof(custom_format),
                             custom_hex, sizeof(custom_hex));

    char body[256];
    snprintf(body, sizeof(body),
             "{\"profile\":\"%s\",\"custom_format\":\"%s\",\"custom_hex\":\"%s\"}",
             profile, custom_format, custom_hex);
    return send_json(req, status_code, body);
}

static esp_err_t info_get_handler(httpd_req_t *req)
{
    if (!ensure_authorized(req)) {
        return ESP_OK;
    }

    char id[8] = {0};
    app_status_device_id(id, sizeof(id));

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    char ip_str[16] = "0.0.0.0";
    esp_netif_ip_info_t ip_info;
    esp_netif_t *sta = app_wifi_sta_netif();
    if (sta && esp_netif_get_ip_info(sta, &ip_info) == ESP_OK) {
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
    }

    char profile[24];
    char custom_format[24];
    char custom_hex[APP_WAKE_PROFILE_MAX_CUSTOM_HEX_LEN + 1];
    load_wake_config_strings(profile, sizeof(profile),
                             custom_format, sizeof(custom_format),
                             custom_hex, sizeof(custom_hex));

    int64_t uptime_us = esp_timer_get_time();

    char body[512];
    snprintf(body, sizeof(body),
             "{\"id\":\"%s\",\"fw\":\"%s\",\"api_version\":%d,"
             "\"chip\":\"%s\",\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\","
             "\"ip\":\"%s\",\"uptime_s\":%" PRId64 ","
             "\"wake_profile\":\"%s\",\"wake_custom_format\":\"%s\","
             "\"wake_custom_hex\":\"%s\"}",
             id, APP_FW_VERSION, APP_WAKE_PROFILE_API_VERSION,
             APP_CHIP_NAME,
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
             ip_str, uptime_us / 1000000,
             profile, custom_format, custom_hex);
    return send_json(req, 200, body);
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    if (!ensure_authorized(req)) {
        return ESP_OK;
    }

    int8_t rssi = 0;
    wifi_ap_record_t ap = {0};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        rssi = ap.rssi;
    }

    int64_t last_us = app_status_last_wake_us();
    int64_t now_us = esp_timer_get_time();
    int64_t since_s = (last_us == 0) ? -1 : (now_us - last_us) / 1000000;

    char body[200];
    snprintf(body, sizeof(body),
             "{\"ble_busy\":%s,\"wake_count\":%" PRIu32 ","
             "\"last_wake_s_ago\":%" PRId64 ","
             "\"rssi\":%d,\"free_heap\":%" PRIu32 "}",
             app_status_ble_busy() ? "true" : "false",
             app_status_wake_count(),
             since_s, rssi,
             (uint32_t)esp_get_free_heap_size());
    return send_json(req, 200, body);
}

static esp_err_t wakeup_post_handler(httpd_req_t *req)
{
    if (!ensure_authorized(req)) {
        return ESP_OK;
    }

    esp_err_t err = app_ble_wakeup_trigger(APP_BLE_WAKE_DEFAULT_DURATION_MS);
    if (err == ESP_ERR_INVALID_STATE) {
        return send_json(req, 409,
                         "{\"started\":false,\"reason\":\"busy\"}");
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wakeup trigger err=%s", esp_err_to_name(err));
        return send_json(req, 500,
                         "{\"started\":false,\"reason\":\"internal\"}");
    }
    return send_json(req, 202,
                     "{\"started\":true,\"duration_ms\":5000}");
}

static esp_err_t wakeup_config_post_handler(httpd_req_t *req)
{
    if (!ensure_authorized(req)) {
        return ESP_OK;
    }

    char body[512];
    esp_err_t err = recv_request_body(req, body, sizeof(body));
    if (err != ESP_OK) {
        return send_json(req, 400, "{\"error\":\"bad_body\"}");
    }

    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        return send_json(req, 400, "{\"error\":\"invalid_json\"}");
    }

    cJSON *profile = cJSON_GetObjectItemCaseSensitive(root, "profile");
    cJSON *custom_format = cJSON_GetObjectItemCaseSensitive(root, "custom_format");
    cJSON *custom_hex = cJSON_GetObjectItemCaseSensitive(root, "custom_hex");
    if (!cJSON_IsString(profile) || !cJSON_IsString(custom_format) ||
        !cJSON_IsString(custom_hex)) {
        cJSON_Delete(root);
        return send_json(req, 400, "{\"error\":\"missing_fields\"}");
    }

    app_wake_profile_config_t config = {
        .profile = APP_WAKE_PROFILE_D5X_PRO,
        .custom_format = APP_WAKE_CUSTOM_FORMAT_MANUFACTURER_DATA,
        .custom_hex = "",
    };

    err = app_wake_profile_parse_profile(profile->valuestring, &config.profile);
    if (err == ESP_OK) {
        err = app_wake_profile_parse_custom_format(
            custom_format->valuestring, &config.custom_format
        );
    }
    if (err == ESP_OK) {
        strncpy(config.custom_hex, custom_hex->valuestring,
                sizeof(config.custom_hex) - 1);
        config.custom_hex[sizeof(config.custom_hex) - 1] = '\0';
        err = app_wake_profile_set_config(&config);
    }

    cJSON_Delete(root);

    if (err == ESP_ERR_INVALID_ARG || err == ESP_ERR_INVALID_SIZE) {
        return send_json(req, 400, "{\"error\":\"invalid_wakeup_config\"}");
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to save wake config: %s", esp_err_to_name(err));
        return send_json(req, 500, "{\"error\":\"wake_config_save_failed\"}");
    }

    return send_wake_config_json(req, 200);
}

static esp_err_t reset_wifi_post_handler(httpd_req_t *req)
{
    if (!ensure_authorized(req)) {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "reset wifi requested via HTTP");
    app_wifi_clear_credentials();
    send_json(req, 200, "{\"ok\":true}");

    esp_timer_handle_t reboot_timer = NULL;
    const esp_timer_create_args_t timer_args = {
        .callback = (void (*)(void *))esp_restart,
        .name = "reboot",
    };
    if (esp_timer_create(&timer_args, &reboot_timer) == ESP_OK) {
        esp_timer_start_once(reboot_timer, 1000ULL * 1000ULL);
    }
    return ESP_OK;
}

esp_err_t app_http_register(httpd_handle_t server)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    static const httpd_uri_t uri_info = {
        .uri = "/api/info",
        .method = HTTP_GET,
        .handler = info_get_handler,
    };
    static const httpd_uri_t uri_status = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = status_get_handler,
    };
    static const httpd_uri_t uri_wakeup = {
        .uri = "/api/wakeup",
        .method = HTTP_POST,
        .handler = wakeup_post_handler,
    };
    static const httpd_uri_t uri_wakeup_config = {
        .uri = "/api/wakeup_config",
        .method = HTTP_POST,
        .handler = wakeup_config_post_handler,
    };
    static const httpd_uri_t uri_reset = {
        .uri = "/api/reset_wifi",
        .method = HTTP_POST,
        .handler = reset_wifi_post_handler,
    };

    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_info));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_status));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_wakeup));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_wakeup_config));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_reset));
    return ESP_OK;
}
