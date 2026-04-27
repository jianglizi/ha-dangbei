#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_WAKE_PROFILE_API_VERSION 3
#define APP_WAKE_PROFILE_MAX_ADV_BYTES 31
#define APP_WAKE_PROFILE_MAX_CUSTOM_HEX_LEN (APP_WAKE_PROFILE_MAX_ADV_BYTES * 2)
#define APP_WAKE_PROFILE_MAX_MANUFACTURER_DATA_BYTES 16
#define APP_WAKE_MAC_LEN 6

typedef enum {
    APP_WAKE_PROFILE_MAC_BASED = 0,
    APP_WAKE_PROFILE_CUSTOM,
} app_wake_profile_kind_t;

typedef enum {
    APP_WAKE_CUSTOM_FORMAT_FULL_ADV = 0,
    APP_WAKE_CUSTOM_FORMAT_MANUFACTURER_DATA,
} app_wake_custom_format_t;

typedef struct {
    app_wake_profile_kind_t profile;
    app_wake_custom_format_t custom_format;
    uint8_t bluetooth_mac[APP_WAKE_MAC_LEN];
    char custom_hex[APP_WAKE_PROFILE_MAX_CUSTOM_HEX_LEN + 1];
} app_wake_profile_config_t;

typedef struct {
    uint8_t adv_data[APP_WAKE_PROFILE_MAX_ADV_BYTES];
    size_t adv_data_len;
    uint8_t conn_mode;
    uint8_t disc_mode;
    uint16_t interval_min;
    uint16_t interval_max;
} app_wake_adv_config_t;

esp_err_t app_wake_profile_init(void);

void app_wake_profile_get_config(app_wake_profile_config_t *out);

esp_err_t app_wake_profile_set_config(const app_wake_profile_config_t *config);

esp_err_t app_wake_profile_build_phase1_config(app_wake_adv_config_t *out);

esp_err_t app_wake_profile_build_phase2_config(app_wake_adv_config_t *out);

const char *app_wake_profile_profile_name(app_wake_profile_kind_t profile);

const char *app_wake_profile_custom_format_name(app_wake_custom_format_t format);

esp_err_t app_wake_profile_parse_profile(
    const char *value,
    app_wake_profile_kind_t *out
);

esp_err_t app_wake_profile_parse_custom_format(
    const char *value,
    app_wake_custom_format_t *out
);

void app_wake_profile_compute_phase1(
    const uint8_t *mac,
    uint8_t *out,
    size_t out_len
);

void app_wake_profile_compute_phase2(
    const uint8_t *mac,
    uint8_t *out,
    size_t out_len
);

#ifdef __cplusplus
}
#endif
