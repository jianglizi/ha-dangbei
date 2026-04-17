#include "app_provision.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "cJSON.h"

#include "app_wifi.h"

static const char *TAG = "provision";

static const char INDEX_HTML[] =
    "<!doctype html>"
    "<html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Dangbei WOL 配网</title>"
    "<style>"
    "body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;"
    "max-width:460px;margin:24px auto;padding:0 16px;color:#222;}"
    "h1{font-size:20px;margin:8px 0 16px;}"
    "label{display:block;margin:12px 0 4px;font-size:14px;color:#555;}"
    "input,select{width:100%;padding:10px;font-size:15px;border:1px solid #ccc;"
    "border-radius:6px;box-sizing:border-box;}"
    "button{margin-top:14px;width:100%;padding:12px;font-size:16px;"
    "background:#2c7be5;color:#fff;border:0;border-radius:6px;}"
    "button.secondary{background:#eef3fb;color:#245;}"
    "small{color:#888;display:block;margin-top:8px;}"
    ".hint{font-size:13px;color:#6b7280;margin-top:4px;}"
    "#scan-status{font-size:13px;color:#6b7280;margin-top:8px;min-height:18px;}"
    "</style></head><body>"
    "<h1>Dangbei 投影仪开机器</h1>"
    "<form method=\"POST\" action=\"/save\">"
    "<label>WiFi 名称 (SSID)</label>"
    "<input id=\"ssid\" name=\"ssid\" required maxlength=\"32\" autocomplete=\"off\">"
    "<label>附近 WiFi（选中后自动填入上面的 SSID）</label>"
    "<select id=\"wifi-select\">"
    "<option value=\"\">请先扫描附近 WiFi</option>"
    "</select>"
    "<button class=\"secondary\" type=\"button\" id=\"scan-btn\">扫描附近 WiFi</button>"
    "<div id=\"scan-status\">正在扫描附近 WiFi...</div>"
    "<label>WiFi 密码</label>"
    "<input name=\"password\" type=\"password\" maxlength=\"63\">"
    "<label>API Bearer Token（可选）</label>"
    "<input name=\"token\" maxlength=\"64\" autocomplete=\"off\">"
    "<div class=\"hint\">留空表示局域网内不鉴权；如填写，HA 调用 REST API 时需带同一个 Token。</div>"
    "<button type=\"submit\">保存并重启</button>"
    "</form>"
    "<p><small>设备会保存 SSID、密码和 Token 到 NVS，并重启连接到家用 WiFi。</small></p>"
    "<script>"
    "const statusEl=document.getElementById('scan-status');"
    "const ssidEl=document.getElementById('ssid');"
    "const selectEl=document.getElementById('wifi-select');"
    "function resetOptions(placeholder){"
    "selectEl.innerHTML='';"
    "const opt=document.createElement('option');"
    "opt.value='';"
    "opt.textContent=placeholder;"
    "selectEl.appendChild(opt);"
    "selectEl.value='';"
    "}"
    "async function scanNetworks(){"
    "statusEl.textContent='正在扫描...';"
    "resetOptions('正在扫描附近 WiFi...');"
    "try{"
    "const resp=await fetch('/scan',{cache:'no-store'});"
    "if(!resp.ok) throw new Error('scan failed');"
    "const data=await resp.json();"
    "const networks=(data.networks||[]);"
    "resetOptions(networks.length?'请选择一个 WiFi':'未扫描到可用 WiFi');"
    "for(const net of networks){"
    "const opt=document.createElement('option');"
    "opt.value=net.ssid;"
    "opt.textContent=`${net.ssid} (${net.rssi} dBm / ${net.auth})`;"
    "selectEl.appendChild(opt);"
    "}"
    "statusEl.textContent=networks.length"
    "?`找到 ${networks.length} 个网络，请从下拉框中选择`:'未扫描到可用 WiFi';"
    "}catch(err){"
    "resetOptions('扫描失败，请稍后重试');"
    "statusEl.textContent='扫描失败，请稍后重试';"
    "}"
    "}"
    "selectEl.addEventListener('change',()=>{"
    "if(selectEl.value){ssidEl.value=selectEl.value;}"
    "});"
    "document.getElementById('scan-btn').addEventListener('click',scanNetworks);"
    "scanNetworks();"
    "</script>"
    "</body></html>";

static const char SAVED_HTML[] =
    "<!doctype html><html><head><meta charset=\"utf-8\">"
    "<title>已保存</title></head><body style=\"font-family:sans-serif;"
    "padding:24px;\"><h2>已保存，正在重启...</h2>"
    "<p>请稍候 10 秒后断开 AP 并连回家用 WiFi。</p></body></html>";

static esp_err_t send_scan_error(httpd_req_t *req, const char *reason)
{
    char body[96];
    snprintf(body, sizeof(body), "{\"error\":\"%s\"}", reason);
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static const char *authmode_to_str(wifi_auth_mode_t authmode)
{
    switch (authmode) {
    case WIFI_AUTH_OPEN:
        return "open";
    case WIFI_AUTH_WEP:
        return "wep";
    case WIFI_AUTH_WPA_PSK:
        return "wpa";
    case WIFI_AUTH_WPA2_PSK:
        return "wpa2";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "wpa/wpa2";
    case WIFI_AUTH_WPA2_ENTERPRISE:
        return "wpa2-enterprise";
    case WIFI_AUTH_WPA3_PSK:
        return "wpa3";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "wpa2/wpa3";
    case WIFI_AUTH_WAPI_PSK:
        return "wapi";
    default:
        return "unknown";
    }
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* x-www-form-urlencoded 解码：% 解码 + '+' 转空格。 */
static void url_decode(char *out, size_t out_len, const char *in)
{
    size_t i = 0;
    while (*in && i + 1 < out_len) {
        if (*in == '+') {
            out[i++] = ' ';
            in++;
        } else if (*in == '%' && in[1] && in[2]) {
            int hi = hex_value(in[1]);
            int lo = hex_value(in[2]);
            if (hi >= 0 && lo >= 0) {
                out[i++] = (char)((hi << 4) | lo);
                in += 3;
            } else {
                out[i++] = *in++;
            }
        } else {
            out[i++] = *in++;
        }
    }
    out[i] = '\0';
}

static bool extract_field(const char *body, const char *key,
                          char *out, size_t out_len)
{
    size_t key_len = strlen(key);
    const char *p = body;
    while (p && *p) {
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            const char *val = p + key_len + 1;
            const char *end = strchr(val, '&');
            size_t n = end ? (size_t)(end - val) : strlen(val);
            char raw[128] = {0};
            if (n >= sizeof(raw)) n = sizeof(raw) - 1;
            memcpy(raw, val, n);
            raw[n] = '\0';
            url_decode(out, out_len, raw);
            return true;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
    return false;
}

static esp_err_t index_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t scan_get_handler(httpd_req_t *req)
{
    app_wifi_scan_result_t networks[16] = {0};
    size_t count = 0;
    esp_err_t err = app_wifi_scan_networks(networks, 16, &count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(err));
        return send_scan_error(req, "scan_failed");
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return send_scan_error(req, "no_mem");
    }

    cJSON *array = cJSON_AddArrayToObject(root, "networks");
    if (array == NULL) {
        cJSON_Delete(root);
        return send_scan_error(req, "no_mem");
    }

    for (size_t idx = 0; idx < count; idx++) {
        cJSON *item = cJSON_CreateObject();
        if (item == NULL) {
            cJSON_Delete(root);
            return send_scan_error(req, "no_mem");
        }
        cJSON_AddStringToObject(item, "ssid", networks[idx].ssid);
        cJSON_AddNumberToObject(item, "rssi", networks[idx].rssi);
        cJSON_AddStringToObject(item, "auth", authmode_to_str(networks[idx].authmode));
        cJSON_AddItemToArray(array, item);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        return send_scan_error(req, "no_mem");
    }

    httpd_resp_set_type(req, "application/json");
    esp_err_t send_err = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    cJSON_free(json);
    return send_err;
}

static esp_err_t save_post_handler(httpd_req_t *req)
{
    char buf[512] = {0};
    int total = 0;
    int remaining = req->content_len;
    if (remaining <= 0 || remaining >= (int)sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_FAIL;
    }
    while (remaining > 0) {
        int r = httpd_req_recv(req, buf + total, remaining);
        if (r <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv");
            return ESP_FAIL;
        }
        total += r;
        remaining -= r;
    }
    buf[total] = '\0';

    char ssid[33] = {0};
    char pass[65] = {0};
    char token[APP_WIFI_MAX_TOKEN_LEN + 1] = {0};
    if (!extract_field(buf, "ssid", ssid, sizeof(ssid)) || ssid[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing ssid");
        return ESP_FAIL;
    }
    extract_field(buf, "password", pass, sizeof(pass));
    extract_field(buf, "token", token, sizeof(token));

    ESP_LOGI(TAG, "saving credentials, SSID=%s", ssid);
    esp_err_t err = app_wifi_save_credentials(ssid, pass, token);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "nvs");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, SAVED_HTML, HTTPD_RESP_USE_STRLEN);

    /* 延迟重启，确保响应被发送出去。 */
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

/* Captive portal：把所有未匹配请求重定向到根。 */
static esp_err_t captive_redirect_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t app_provision_register(httpd_handle_t server)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    static const httpd_uri_t uri_index = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_get_handler,
    };
    static const httpd_uri_t uri_scan = {
        .uri = "/scan",
        .method = HTTP_GET,
        .handler = scan_get_handler,
    };
    static const httpd_uri_t uri_save = {
        .uri = "/save",
        .method = HTTP_POST,
        .handler = save_post_handler,
    };
    static const httpd_uri_t uri_catch = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = captive_redirect_handler,
    };

    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_index));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_scan));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_save));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_catch));
    return ESP_OK;
}
