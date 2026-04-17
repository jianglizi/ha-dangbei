#include "app_ble_wakeup.h"

#include <inttypes.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/util/util.h"

#include "app_status.h"
#include "app_wake_profile.h"

static const char *TAG = "ble_wake";

static SemaphoreHandle_t s_sync_sem;
static SemaphoreHandle_t s_busy_lock;
static esp_timer_handle_t s_stop_timer;
static volatile bool s_host_ready;

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_util_ensure_addr failed: %d", rc);
    }
    s_host_ready = true;
    if (s_sync_sem != NULL) {
        xSemaphoreGive(s_sync_sem);
    }
    ESP_LOGI(TAG, "NimBLE host synced");
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE host reset, reason=%d", reason);
    s_host_ready = false;
}

static void ble_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void stop_timer_cb(void *arg)
{
    (void)arg;
    int rc = ble_gap_adv_stop();
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGW(TAG, "ble_gap_adv_stop rc=%d", rc);
    }
    app_status_set_ble_busy(false);
    if (s_busy_lock != NULL) {
        xSemaphoreGive(s_busy_lock);
    }
    ESP_LOGI(TAG, "wake advertising stopped");
}

esp_err_t app_ble_wakeup_init(void)
{
    s_sync_sem = xSemaphoreCreateBinary();
    s_busy_lock = xSemaphoreCreateBinary();
    if (s_sync_sem == NULL || s_busy_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreGive(s_busy_lock);

    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    const esp_timer_create_args_t timer_args = {
        .callback = stop_timer_cb,
        .name = "ble_wake_stop",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_stop_timer));

    nimble_port_freertos_init(ble_host_task);
    return ESP_OK;
}

esp_err_t app_ble_wakeup_trigger(uint32_t duration_ms)
{
    esp_err_t ret;

    if (duration_ms == 0) {
        duration_ms = APP_BLE_WAKE_DEFAULT_DURATION_MS;
    }

    /* 等待 host 同步（首次启动可能尚未就绪）。 */
    if (!s_host_ready) {
        if (xSemaphoreTake(s_sync_sem, pdMS_TO_TICKS(3000)) != pdTRUE) {
            ESP_LOGE(TAG, "ble host not ready");
            return ESP_ERR_TIMEOUT;
        }
        xSemaphoreGive(s_sync_sem);
    }

    /* 互斥：广播期间拒绝重入。 */
    if (xSemaphoreTake(s_busy_lock, 0) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    app_wake_adv_config_t adv_config = {0};
    ret = app_wake_profile_build_adv_config(&adv_config);
    if (ret != ESP_OK) {
        xSemaphoreGive(s_busy_lock);
        ESP_LOGE(TAG, "app_wake_profile_build_adv_config failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    int rc = ble_gap_adv_set_data(adv_config.adv_data, adv_config.adv_data_len);
    if (rc != 0) {
        xSemaphoreGive(s_busy_lock);
        ESP_LOGE(TAG, "ble_gap_adv_set_data rc=%d", rc);
        return ESP_FAIL;
    }

    struct ble_gap_adv_params adv_params = {
        .conn_mode = adv_config.conn_mode,
        .disc_mode = adv_config.disc_mode,
        .itvl_min = adv_config.interval_min,
        .itvl_max = adv_config.interval_max,
    };

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, NULL, NULL);
    if (rc != 0) {
        xSemaphoreGive(s_busy_lock);
        ESP_LOGE(TAG, "ble_gap_adv_start rc=%d", rc);
        return ESP_FAIL;
    }

    app_status_set_ble_busy(true);
    app_status_record_wake();
    app_wake_profile_config_t wake_config;
    app_wake_profile_get_config(&wake_config);
    ESP_LOGI(TAG, "wake advertising started, profile=%s, duration=%" PRIu32 " ms",
             app_wake_profile_profile_name(wake_config.profile),
             duration_ms);

    esp_timer_stop(s_stop_timer);
    ESP_ERROR_CHECK(esp_timer_start_once(s_stop_timer,
                                         (uint64_t)duration_ms * 1000ULL));
    return ESP_OK;
}
