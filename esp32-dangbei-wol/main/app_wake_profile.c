#include "app_wake_profile.h"

#include <ctype.h>
#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_gap.h"
#include "nvs.h"

#include "app_status.h"

static const char *TAG = "wake_profile";

#define NVS_NAMESPACE "wake"
#define NVS_KEY_PROFILE "profile"
#define NVS_KEY_FORMAT  "format"
#define NVS_KEY_HEX     "hex"
#define NVS_KEY_MAC     "btmac"

#define ADV_INTERVAL_MIN 0x20
#define ADV_INTERVAL_MAX 0x40

/* Phase 1 service UUID: 0x1812 (Human Interface Device) */
#define PHASE1_SERVICE_UUID 0x1812
/* Phase 1 manufacturer data ID: 0x0046 (70) */
#define PHASE1_MFG_ID 0x0046

/* Phase 2 service UUID: 0xB001 */
#define PHASE2_SERVICE_UUID 0xB001
/* Phase 2 manufacturer data ID: 0x013B (315) */
#define PHASE2_MFG_ID 0x013B

static SemaphoreHandle_t s_lock;
static app_wake_profile_config_t s_config;

static app_wake_profile_config_t default_config(void)
{
    app_wake_profile_config_t config = {
        .profile = APP_WAKE_PROFILE_MAC_BASED,
        .custom_format = APP_WAKE_CUSTOM_FORMAT_MANUFACTURER_DATA,
        .bluetooth_mac = {0},
        .custom_hex = "",
    };
    return config;
}

static esp_err_t load_config_from_nvs(app_wake_profile_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *config = default_config();
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    *config = default_config();

    int32_t profile = 0;
    err = nvs_get_i32(handle, NVS_KEY_PROFILE, &profile);
    if (err == ESP_OK) {
        config->profile = (app_wake_profile_kind_t)profile;
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return err;
    }

    int32_t format = 0;
    err = nvs_get_i32(handle, NVS_KEY_FORMAT, &format);
    if (err == ESP_OK) {
        config->custom_format = (app_wake_custom_format_t)format;
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return err;
    }

    size_t hex_len = sizeof(config->custom_hex);
    err = nvs_get_str(handle, NVS_KEY_HEX, config->custom_hex, &hex_len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        config->custom_hex[0] = '\0';
        err = ESP_OK;
    }

    size_t mac_len = APP_WAKE_MAC_LEN;
    err = nvs_get_blob(handle, NVS_KEY_MAC, config->bluetooth_mac, &mac_len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        memset(config->bluetooth_mac, 0, APP_WAKE_MAC_LEN);
        err = ESP_OK;
    }

    nvs_close(handle);
    return err;
}

static esp_err_t save_config_to_nvs(const app_wake_profile_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_i32(handle, NVS_KEY_PROFILE, (int32_t)config->profile);
    if (err == ESP_OK) {
        err = nvs_set_i32(handle, NVS_KEY_FORMAT, (int32_t)config->custom_format);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, NVS_KEY_HEX, config->custom_hex);
    }
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, NVS_KEY_MAC, config->bluetooth_mac,
                           APP_WAKE_MAC_LEN);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

static void normalize_hex_string(const char *input, char *output, size_t output_len)
{
    size_t written = 0;

    if (output_len == 0) {
        return;
    }

    output[0] = '\0';
    if (input == NULL) {
        return;
    }

    for (size_t idx = 0; input[idx] != '\0' && written + 1 < output_len; idx++) {
        if (isspace((unsigned char)input[idx])) {
            continue;
        }
        output[written++] = (char)tolower((unsigned char)input[idx]);
    }
    output[written] = '\0';
}

static bool is_valid_hex_string(const char *value)
{
    if (value == NULL) {
        return false;
    }

    for (size_t idx = 0; value[idx] != '\0'; idx++) {
        if (!isxdigit((unsigned char)value[idx])) {
            return false;
        }
    }
    return true;
}

static int hex_char_to_nibble(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return 10 + (value - 'a');
    }
    return -1;
}

static esp_err_t decode_hex(const char *value, uint8_t *out, size_t out_size, size_t *out_len)
{
    size_t value_len = strlen(value);

    if ((value_len % 2) != 0 || out == NULL || out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((value_len / 2) > out_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    for (size_t idx = 0; idx < value_len; idx += 2) {
        int high = hex_char_to_nibble(value[idx]);
        int low = hex_char_to_nibble(value[idx + 1]);
        if (high < 0 || low < 0) {
            return ESP_ERR_INVALID_ARG;
        }
        out[idx / 2] = (uint8_t)((high << 4) | low);
    }

    *out_len = value_len / 2;
    return ESP_OK;
}

/* Parse "AA:BB:CC:DD:EE:FF" into 6 bytes. */
static esp_err_t parse_mac_string(const char *str, uint8_t *out)
{
    if (str == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    unsigned int vals[6] = {0};
    int matched = sscanf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
                         &vals[0], &vals[1], &vals[2],
                         &vals[3], &vals[4], &vals[5]);
    if (matched != 6) {
        return ESP_ERR_INVALID_ARG;
    }
    for (int i = 0; i < 6; i++) {
        out[i] = (uint8_t)vals[i];
    }
    return ESP_OK;
}

static bool is_zero_mac(const uint8_t *mac)
{
    for (int i = 0; i < APP_WAKE_MAC_LEN; i++) {
        if (mac[i] != 0) {
            return false;
        }
    }
    return true;
}

static esp_err_t validate_config(
    const app_wake_profile_config_t *input,
    app_wake_profile_config_t *normalized
)
{
    if (input == NULL || normalized == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (input->profile > APP_WAKE_PROFILE_CUSTOM ||
        input->custom_format > APP_WAKE_CUSTOM_FORMAT_MANUFACTURER_DATA) {
        return ESP_ERR_INVALID_ARG;
    }

    *normalized = *input;
    normalize_hex_string(input->custom_hex, normalized->custom_hex,
                         sizeof(normalized->custom_hex));

    if (normalized->profile != APP_WAKE_PROFILE_CUSTOM) {
        return ESP_OK;
    }

    size_t hex_len = strlen(normalized->custom_hex);
    if (hex_len == 0 || (hex_len % 2) != 0 || !is_valid_hex_string(normalized->custom_hex)) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t max_bytes = (
        normalized->custom_format == APP_WAKE_CUSTOM_FORMAT_FULL_ADV
            ? APP_WAKE_PROFILE_MAX_ADV_BYTES
            : APP_WAKE_PROFILE_MAX_MANUFACTURER_DATA_BYTES
    );

    if ((hex_len / 2) > max_bytes) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

/*
 * CommonUtil.k() — Phase 1 payload (11 bytes):
 *   out[0] = 0x00
 *   out[1..6] = reversed MAC (mac[5], mac[4], ..., mac[0])
 *   out[7..10] = 0xFF, 0xFF, 0xFF, 0xFF
 */
void app_wake_profile_compute_phase1(
    const uint8_t *mac,
    uint8_t *out,
    size_t out_len
)
{
    if (out == NULL || out_len < 11 || mac == NULL) {
        return;
    }
    memset(out, 0xFF, 11);
    out[0] = 0x00;
    for (int i = 0; i < 6; i++) {
        out[1 + i] = mac[5 - i];
    }
    out[7] = 0xFF;
    out[8] = 0xFF;
    out[9] = 0xFF;
    out[10] = 0xFF;
}

/*
 * CommonUtil.l() — Phase 2 payload (6 bytes):
 *   MAC in original order: mac[0], mac[1], ..., mac[5]
 */
void app_wake_profile_compute_phase2(
    const uint8_t *mac,
    uint8_t *out,
    size_t out_len
)
{
    if (out == NULL || out_len < 6 || mac == NULL) {
        return;
    }
    memcpy(out, mac, 6);
}

static void build_adv_data_with_service_and_mfg(
    uint8_t *adv,
    size_t *adv_len,
    uint16_t service_uuid,
    uint16_t mfg_id,
    const uint8_t *mfg_payload,
    size_t mfg_payload_len
)
{
    size_t pos = 0;

    /* Flags: type 0x01, data 0x05 (LE General Discoverable + BR/EDR Not Supported) */
    adv[pos++] = 0x02; /* length */
    adv[pos++] = 0x01; /* type: Flags */
    adv[pos++] = 0x05; /* data */

    /* Service UUID (16-bit): type 0x03 */
    adv[pos++] = 0x03; /* length */
    adv[pos++] = 0x03; /* type: Complete List of 16-bit Service UUIDs */
    adv[pos++] = (uint8_t)(service_uuid & 0xFF);
    adv[pos++] = (uint8_t)((service_uuid >> 8) & 0xFF);

    /* Manufacturer Specific Data: type 0xFF */
    size_t mfg_total = 2 + mfg_payload_len; /* mfg_id(2) + payload */
    adv[pos++] = (uint8_t)(mfg_total + 1); /* length = type(1) + mfg_id(2) + payload */
    adv[pos++] = 0xFF; /* type: Manufacturer Specific Data */
    adv[pos++] = (uint8_t)(mfg_id & 0xFF);
    adv[pos++] = (uint8_t)((mfg_id >> 8) & 0xFF);
    memcpy(adv + pos, mfg_payload, mfg_payload_len);
    pos += mfg_payload_len;

    *adv_len = pos;
}

esp_err_t app_wake_profile_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    app_wake_profile_config_t loaded = default_config();
    esp_err_t err = load_config_from_nvs(&loaded);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to load wake profile config, using defaults: %s",
                 esp_err_to_name(err));
        loaded = default_config();
    }

    app_wake_profile_config_t normalized = default_config();
    err = validate_config(&loaded, &normalized);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "stored wake profile config invalid, resetting to defaults");
        normalized = default_config();
        err = save_config_to_nvs(&normalized);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "failed to persist default wake profile: %s",
                     esp_err_to_name(err));
        }
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_config = normalized;
    xSemaphoreGive(s_lock);

    ESP_LOGI(TAG, "wake profile initialized: profile=%s",
             app_wake_profile_profile_name(s_config.profile));
    return ESP_OK;
}

void app_wake_profile_get_config(app_wake_profile_config_t *out)
{
    if (out == NULL) {
        return;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_config;
    xSemaphoreGive(s_lock);
}

esp_err_t app_wake_profile_set_config(const app_wake_profile_config_t *config)
{
    app_wake_profile_config_t normalized = default_config();
    esp_err_t err = validate_config(config, &normalized);
    if (err != ESP_OK) {
        return err;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    err = save_config_to_nvs(&normalized);
    if (err == ESP_OK) {
        s_config = normalized;
    }
    xSemaphoreGive(s_lock);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "wake profile updated: profile=%s",
                 app_wake_profile_profile_name(normalized.profile));
    }

    return err;
}

esp_err_t app_wake_profile_build_phase1_config(app_wake_adv_config_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    app_wake_profile_config_t config;
    app_wake_profile_get_config(&config);
    memset(out, 0, sizeof(*out));

    if (config.profile == APP_WAKE_PROFILE_MAC_BASED) {
        if (is_zero_mac(config.bluetooth_mac)) {
            ESP_LOGE(TAG, "mac_based profile but bluetooth_mac is zero");
            return ESP_ERR_INVALID_STATE;
        }

        uint8_t mfg_payload[11];
        app_wake_profile_compute_phase1(config.bluetooth_mac, mfg_payload, 11);

        build_adv_data_with_service_and_mfg(
            out->adv_data, &out->adv_data_len,
            PHASE1_SERVICE_UUID, PHASE1_MFG_ID,
            mfg_payload, 11
        );
        out->conn_mode = BLE_GAP_CONN_MODE_UND;
        out->disc_mode = BLE_GAP_DISC_MODE_LTD;
        out->interval_min = ADV_INTERVAL_MIN;
        out->interval_max = ADV_INTERVAL_MAX;
        return ESP_OK;
    }

    /* Custom profile: build from custom_hex as manufacturer data */
    if (config.custom_format == APP_WAKE_CUSTOM_FORMAT_FULL_ADV) {
        size_t adv_len = 0;
        esp_err_t err = decode_hex(config.custom_hex, out->adv_data,
                                   sizeof(out->adv_data), &adv_len);
        if (err != ESP_OK) {
            return err;
        }
        out->adv_data_len = adv_len;
        out->conn_mode = BLE_GAP_CONN_MODE_UND;
        out->disc_mode = BLE_GAP_DISC_MODE_LTD;
        out->interval_min = ADV_INTERVAL_MIN;
        out->interval_max = ADV_INTERVAL_MAX;
        return ESP_OK;
    }

    /* Custom manufacturer data format */
    uint8_t mfg_data[APP_WAKE_PROFILE_MAX_MANUFACTURER_DATA_BYTES];
    size_t mfg_len = 0;
    esp_err_t err = decode_hex(config.custom_hex, mfg_data, sizeof(mfg_data), &mfg_len);
    if (err != ESP_OK) {
        return err;
    }

    /* Build with generic service UUID 0x1812 and mfg ID from first 2 bytes if present */
    uint16_t mfg_id = 0x0046;
    const uint8_t *payload = mfg_data;
    size_t payload_len = mfg_len;
    if (mfg_len >= 2) {
        mfg_id = (uint16_t)(mfg_data[0] | (mfg_data[1] << 8));
        payload = mfg_data + 2;
        payload_len = mfg_len - 2;
    }

    build_adv_data_with_service_and_mfg(
        out->adv_data, &out->adv_data_len,
        PHASE1_SERVICE_UUID, mfg_id,
        payload, payload_len
    );
    out->conn_mode = BLE_GAP_CONN_MODE_UND;
    out->disc_mode = BLE_GAP_DISC_MODE_LTD;
    out->interval_min = ADV_INTERVAL_MIN;
    out->interval_max = ADV_INTERVAL_MAX;
    return ESP_OK;
}

esp_err_t app_wake_profile_build_phase2_config(app_wake_adv_config_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    app_wake_profile_config_t config;
    app_wake_profile_get_config(&config);
    memset(out, 0, sizeof(*out));

    if (config.profile != APP_WAKE_PROFILE_MAC_BASED) {
        /* Non-mac_based profiles don't have a phase 2 */
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (is_zero_mac(config.bluetooth_mac)) {
        ESP_LOGE(TAG, "mac_based profile but bluetooth_mac is zero");
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t mfg_payload[6];
    app_wake_profile_compute_phase2(config.bluetooth_mac, mfg_payload, 6);

    build_adv_data_with_service_and_mfg(
        out->adv_data, &out->adv_data_len,
        PHASE2_SERVICE_UUID, PHASE2_MFG_ID,
        mfg_payload, 6
    );
    out->conn_mode = BLE_GAP_CONN_MODE_UND;
    out->disc_mode = BLE_GAP_DISC_MODE_LTD;
    out->interval_min = ADV_INTERVAL_MIN;
    out->interval_max = ADV_INTERVAL_MAX;
    return ESP_OK;
}

const char *app_wake_profile_profile_name(app_wake_profile_kind_t profile)
{
    switch (profile) {
    case APP_WAKE_PROFILE_MAC_BASED:
        return "mac_based";
    case APP_WAKE_PROFILE_CUSTOM:
        return "custom";
    default:
        return "unknown";
    }
}

const char *app_wake_profile_custom_format_name(app_wake_custom_format_t format)
{
    switch (format) {
    case APP_WAKE_CUSTOM_FORMAT_FULL_ADV:
        return "full_adv";
    case APP_WAKE_CUSTOM_FORMAT_MANUFACTURER_DATA:
        return "manufacturer_data";
    default:
        return "unknown";
    }
}

esp_err_t app_wake_profile_parse_profile(
    const char *value,
    app_wake_profile_kind_t *out
)
{
    if (value == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strcmp(value, "mac_based") == 0) {
        *out = APP_WAKE_PROFILE_MAC_BASED;
        return ESP_OK;
    }
    if (strcmp(value, "custom") == 0) {
        *out = APP_WAKE_PROFILE_CUSTOM;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_ARG;
}

esp_err_t app_wake_profile_parse_custom_format(
    const char *value,
    app_wake_custom_format_t *out
)
{
    if (value == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strcmp(value, "full_adv") == 0) {
        *out = APP_WAKE_CUSTOM_FORMAT_FULL_ADV;
        return ESP_OK;
    }
    if (strcmp(value, "manufacturer_data") == 0) {
        *out = APP_WAKE_CUSTOM_FORMAT_MANUFACTURER_DATA;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_ARG;
}
