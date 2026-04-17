#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_FW_VERSION "0.3.0"
#define APP_CHIP_NAME  "esp32c3"

void app_status_init(void);

void app_status_record_wake(void);

uint32_t app_status_wake_count(void);

int64_t app_status_last_wake_us(void);

void app_status_set_ble_busy(bool busy);

bool app_status_ble_busy(void);

/* MAC 后 6 位 hex（不带分隔符），形如 "a1b2c3"，缓冲长度至少 7 字节。 */
void app_status_device_id(char *out, size_t len);

#ifdef __cplusplus
}
#endif
