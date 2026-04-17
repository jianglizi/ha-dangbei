#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_BLE_WAKE_DEFAULT_DURATION_MS 5000

esp_err_t app_ble_wakeup_init(void);

/* 启动一次唤醒广播，duration_ms 内停止。重入会拒绝并返回 ESP_ERR_INVALID_STATE。 */
esp_err_t app_ble_wakeup_trigger(uint32_t duration_ms);

#ifdef __cplusplus
}
#endif
