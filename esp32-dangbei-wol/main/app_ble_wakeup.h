#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Phase 1 advertising duration (ms). Phase 2 runs until projector connects. */
#define APP_BLE_WAKE_PHASE1_DURATION_MS 5000

/* GATT service and characteristic UUIDs (Dangbei projector protocol). */
#define APP_BLE_GATT_SVC_UUID    "0000A000-0000-1000-8000-00805F9B34FB"
#define APP_BLE_GATT_SEND_UUID   "0000A020-0000-1000-8000-00805F9B34FB"
#define APP_BLE_GATT_CLOSE_UUID  "0000A010-0000-1000-8000-00805F9B34FB"
#define APP_BLE_GATT_HEARTBEAT_UUID "0000A030-0000-1000-8000-00805F9B34FB"

esp_err_t app_ble_wakeup_init(void);

/* Start a two-phase wake-up sequence:
 *   Phase 1: advertise with Service 0x1812 for APP_BLE_WAKE_PHASE1_DURATION_MS
 *   Phase 2: advertise with Service 0xB001 until the projector connects
 * Returns ESP_ERR_INVALID_STATE if already running. */
esp_err_t app_ble_wakeup_trigger(void);

/* Whether the BLE subsystem is currently advertising or connected. */
bool app_ble_wakeup_is_busy(void);

#ifdef __cplusplus
}
#endif
