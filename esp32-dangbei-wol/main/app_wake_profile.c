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

#define ADV_INTERVAL_MIN 0x20
#define ADV_INTERVAL_MAX 0x40

static const uint8_t D5X_PRO_ADV_TEMPLATE[] = {
    0x02, 0x01, 0x05,
    0x05, 0x02, 0x0F, 0x18, 0x12, 0x18,
    0x0E, 0xFF, 0x46, 0x00, 0x61, 0x36, 0x51, 0x48,
    0x1D, 0x3E, 0x84, 0xFF, 0xFF, 0xFF, 0xFF,
};

static const uint8_t F3_AIR_ADV_TEMPLATE[] = {
    0x02, 0x01, 0x05,
    0x05, 0x02, 0x0F, 0x18, 0x12, 0x18,
    0x03, 0x19, 0xC1, 0x03,
    0x0E, 0xFF, 0x46, 0x00, 0x46, 0xFA, 0xC1, 0x69,
    0x04, 0xC8, 0x38, 0xFF, 0xFF, 0xFF, 0xFF,
};

static SemaphoreHandle_t s_lock;
static app_wake_profile_config_t s_config;

static app_wake_profile_config_t default_config(void)
{
    app_wake_profile_config_t config = {
        .profile = APP_WAKE_PROFILE_D5X_PRO,
        .custom_format = APP_WAKE_CUSTOM_FORMAT_MANUFACTURER_DATA,
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

static uint8_t next_d5x_seq(void)
{
    return (uint8_t)(0x61u + (app_status_wake_count() & 0xFFu));
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

static void copy_adv_common(
    app_wake_adv_config_t *out,
    const uint8_t *data,
    size_t data_len,
    uint8_t conn_mode
)
{
    memcpy(out->adv_data, data, data_len);
    out->adv_data_len = data_len;
    out->conn_mode = conn_mode;
    out->disc_mode = BLE_GAP_DISC_MODE_LTD;
    out->interval_min = ADV_INTERVAL_MIN;
    out->interval_max = ADV_INTERVAL_MAX;
}

static esp_err_t build_custom_manufacturer_adv(
    const app_wake_profile_config_t *config,
    app_wake_adv_config_t *out
)
{
    uint8_t manufacturer_data[APP_WAKE_PROFILE_MAX_MANUFACTURER_DATA_BYTES];
    size_t manufacturer_len = 0;
    esp_err_t err = decode_hex(config->custom_hex, manufacturer_data,
                               sizeof(manufacturer_data), &manufacturer_len);
    if (err != ESP_OK) {
        return err;
    }

    const uint8_t prefix[] = {
        0x02, 0x01, 0x05,
        0x05, 0x02, 0x0F, 0x18, 0x12, 0x18,
        0x03, 0x19, 0xC1, 0x03,
    };
    size_t required_len = sizeof(prefix) + 2 + manufacturer_len;
    if (required_len > sizeof(out->adv_data)) {
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(out->adv_data, prefix, sizeof(prefix));
    out->adv_data[sizeof(prefix)] = (uint8_t)(manufacturer_len + 1);
    out->adv_data[sizeof(prefix) + 1] = 0xFF;
    memcpy(out->adv_data + sizeof(prefix) + 2, manufacturer_data, manufacturer_len);
    out->adv_data_len = required_len;
    out->conn_mode = BLE_GAP_CONN_MODE_UND;
    out->disc_mode = BLE_GAP_DISC_MODE_LTD;
    out->interval_min = ADV_INTERVAL_MIN;
    out->interval_max = ADV_INTERVAL_MAX;
    return ESP_OK;
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

    ESP_LOGI(TAG, "wake profile initialized: profile=%s format=%s",
             app_wake_profile_profile_name(s_config.profile),
             app_wake_profile_custom_format_name(s_config.custom_format));
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
        ESP_LOGI(TAG, "wake profile updated: profile=%s format=%s hex=%s",
                 app_wake_profile_profile_name(normalized.profile),
                 app_wake_profile_custom_format_name(normalized.custom_format),
                 normalized.custom_hex);
    }

    return err;
}

esp_err_t app_wake_profile_build_adv_config(app_wake_adv_config_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    app_wake_profile_config_t config;
    app_wake_profile_get_config(&config);
    memset(out, 0, sizeof(*out));

    switch (config.profile) {
    case APP_WAKE_PROFILE_D5X_PRO:
        copy_adv_common(out, D5X_PRO_ADV_TEMPLATE, sizeof(D5X_PRO_ADV_TEMPLATE),
                        BLE_GAP_CONN_MODE_NON);
        out->adv_data[13] = next_d5x_seq();
        return ESP_OK;

    case APP_WAKE_PROFILE_F3_AIR:
        copy_adv_common(out, F3_AIR_ADV_TEMPLATE, sizeof(F3_AIR_ADV_TEMPLATE),
                        BLE_GAP_CONN_MODE_UND);
        return ESP_OK;

    case APP_WAKE_PROFILE_CUSTOM:
        if (config.custom_format == APP_WAKE_CUSTOM_FORMAT_FULL_ADV) {
            size_t adv_len = 0;
            esp_err_t err = decode_hex(config.custom_hex, out->adv_data,
                                       sizeof(out->adv_data), &adv_len);
            if (err != ESP_OK) {
                return err;
            }
            out->adv_data_len = adv_len;
            out->conn_mode = BLE_GAP_CONN_MODE_NON;
            out->disc_mode = BLE_GAP_DISC_MODE_LTD;
            out->interval_min = ADV_INTERVAL_MIN;
            out->interval_max = ADV_INTERVAL_MAX;
            return ESP_OK;
        }
        return build_custom_manufacturer_adv(&config, out);

    default:
        return ESP_ERR_INVALID_STATE;
    }
}

const char *app_wake_profile_profile_name(app_wake_profile_kind_t profile)
{
    switch (profile) {
    case APP_WAKE_PROFILE_D5X_PRO:
        return "d5x_pro";
    case APP_WAKE_PROFILE_F3_AIR:
        return "f3_air";
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

    if (strcmp(value, "d5x_pro") == 0) {
        *out = APP_WAKE_PROFILE_D5X_PRO;
        return ESP_OK;
    }
    if (strcmp(value, "f3_air") == 0) {
        *out = APP_WAKE_PROFILE_F3_AIR;
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
