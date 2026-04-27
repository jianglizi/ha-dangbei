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

/* ── State machine ───────────────────────────────────────────────── */

typedef enum {
    BLE_STATE_IDLE = 0,
    BLE_STATE_PHASE1,
    BLE_STATE_PHASE2,
    BLE_STATE_CONNECTED,
} ble_state_t;

static SemaphoreHandle_t s_sync_sem;
static SemaphoreHandle_t s_busy_lock;
static esp_timer_handle_t s_phase1_timer;
static volatile bool s_host_ready;
static volatile ble_state_t s_state = BLE_STATE_IDLE;
static volatile uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;

/* ── NimBLE callbacks ────────────────────────────────────────────── */

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
    s_state = BLE_STATE_IDLE;
}

static void ble_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* ── Phase 1 timer ───────────────────────────────────────────────── */

static void phase1_timer_cb(void *arg);
static void start_phase2(void);

static void phase1_timer_cb(void *arg)
{
    (void)arg;
    if (s_state != BLE_STATE_PHASE1) {
        return;
    }
    ESP_LOGI(TAG, "Phase 1 timeout, switching to Phase 2");
    start_phase2();
}

/* ── GAP event handler ───────────────────────────────────────────── */

static int gap_event_handler(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            ESP_LOGI(TAG, "GATT connected, handle=%d", event->connect.conn_handle);
            s_conn_handle = event->connect.conn_handle;
            s_state = BLE_STATE_CONNECTED;
            /* Stop advertising */
            ble_gap_adv_stop();
            app_status_set_ble_busy(true);
        } else {
            ESP_LOGW(TAG, "Connection failed, status=%d", event->connect.status);
            /* Restart Phase 2 advertising */
            if (s_state == BLE_STATE_PHASE2) {
                start_phase2();
            }
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "GATT disconnected, reason=%d", event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_state = BLE_STATE_IDLE;
        app_status_set_ble_busy(false);
        xSemaphoreGive(s_busy_lock);
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGD(TAG, "Advertising complete");
        break;

    default:
        break;
    }

    return 0;
}

/* ── GATT service ────────────────────────────────────────────────── */

static int gatt_svr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg);

/* Service UUID: 0000A000-0000-1000-8000-00805F9B34FB */
static const ble_uuid128_t gatt_svc_uuid =
    BLE_UUID128_INIT(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
                     0x00, 0x10, 0x00, 0x00, 0x00, 0xA0, 0x00, 0x00);

/* Send characteristic: 0000A020-0000-1000-8000-00805F9B34FB */
static const ble_uuid128_t gatt_send_uuid =
    BLE_UUID128_INIT(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
                     0x00, 0x10, 0x00, 0x00, 0x20, 0xA0, 0x00, 0x00);

/* Close characteristic: 0000A010-0000-1000-8000-00805F9B34FB */
static const ble_uuid128_t gatt_close_uuid =
    BLE_UUID128_INIT(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
                     0x00, 0x10, 0x00, 0x00, 0x10, 0xA0, 0x00, 0x00);

/* Heartbeat characteristic: 0000A030-0000-1000-8000-00805F9B34FB */
static const ble_uuid128_t gatt_heartbeat_uuid =
    BLE_UUID128_INIT(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
                     0x00, 0x10, 0x00, 0x00, 0x30, 0xA0, 0x00, 0x00);

static uint8_t s_heartbeat_val = 0;

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &gatt_send_uuid.u,
                .access_cb = gatt_svr_access_cb,
                .arg = (void *)1,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = &gatt_close_uuid.u,
                .access_cb = gatt_svr_access_cb,
                .arg = (void *)2,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = &gatt_heartbeat_uuid.u,
                .access_cb = gatt_svr_access_cb,
                .arg = (void *)3,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {0}, /* terminator */
        },
    },
    {0}, /* terminator */
};

static int gatt_svr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int id = (int)(uintptr_t)arg;

    switch (id) {
    case 1: /* Send */
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            ESP_LOGI(TAG, "GATT Send write, len=%d", ctxt->om->om_len);
            /* TODO: forward data to projector if needed */
        }
        return 0;

    case 2: /* Close */
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            ESP_LOGI(TAG, "GATT Close write");
            ble_gap_terminate(conn_handle, BLE_ERR_CONN_TERM_LOCAL);
        }
        return 0;

    case 3: /* Heartbeat */
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            s_heartbeat_val++;
            int rc = os_mbuf_append(ctxt->om, &s_heartbeat_val, 1);
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return 0;

    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int gatt_svr_init(void)
{
    int rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc != 0) {
        return rc;
    }
    rc = ble_gatts_add_svcs(gatt_svcs);
    if (rc != 0) {
        return rc;
    }
    return 0;
}

/* ── Advertising helpers ─────────────────────────────────────────── */

static esp_err_t start_advertising(const app_wake_adv_config_t *adv_config)
{
    int rc = ble_gap_adv_set_data(adv_config->adv_data, adv_config->adv_data_len);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_data rc=%d", rc);
        return ESP_FAIL;
    }

    struct ble_gap_adv_params adv_params = {
        .conn_mode = adv_config->conn_mode,
        .disc_mode = adv_config->disc_mode,
        .itvl_min = adv_config->interval_min,
        .itvl_max = adv_config->interval_max,
    };

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event_handler, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_start rc=%d", rc);
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void start_phase2(void)
{
    app_wake_adv_config_t phase2_config;
    esp_err_t err = app_wake_profile_build_phase2_config(&phase2_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Phase 2 build failed: %s, falling back to Phase 1 config",
                 esp_err_to_name(err));
        /* Fall back: just use phase 1 config again */
        err = app_wake_profile_build_phase1_config(&phase2_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Phase 1 fallback also failed: %s", esp_err_to_name(err));
            s_state = BLE_STATE_IDLE;
            app_status_set_ble_busy(false);
            xSemaphoreGive(s_busy_lock);
            return;
        }
    }

    err = start_advertising(&phase2_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Phase 2 advertising start failed");
        s_state = BLE_STATE_IDLE;
        app_status_set_ble_busy(false);
        xSemaphoreGive(s_busy_lock);
        return;
    }

    s_state = BLE_STATE_PHASE2;
    app_status_set_ble_busy(true);
    ESP_LOGI(TAG, "Phase 2 advertising started (Service 0xB001, waiting for connection)");
}

/* ── Public API ──────────────────────────────────────────────────── */

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

    /* Initialize GATT server */
    int rc = gatt_svr_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "gatt_svr_init failed: %d", rc);
        return ESP_FAIL;
    }

    const esp_timer_create_args_t timer_args = {
        .callback = phase1_timer_cb,
        .name = "ble_phase1",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_phase1_timer));

    nimble_port_freertos_init(ble_host_task);
    return ESP_OK;
}

esp_err_t app_ble_wakeup_trigger(void)
{
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

    app_wake_adv_config_t phase1_config;
    esp_err_t err = app_wake_profile_build_phase1_config(&phase1_config);
    if (err != ESP_OK) {
        xSemaphoreGive(s_busy_lock);
        ESP_LOGE(TAG, "Phase 1 build failed: %s", esp_err_to_name(err));
        return err;
    }

    err = start_advertising(&phase1_config);
    if (err != ESP_OK) {
        xSemaphoreGive(s_busy_lock);
        return err;
    }

    s_state = BLE_STATE_PHASE1;
    app_status_set_ble_busy(true);
    app_status_record_wake();

    app_wake_profile_config_t wake_config;
    app_wake_profile_get_config(&wake_config);
    ESP_LOGI(TAG, "Phase 1 advertising started (Service 0x1812), profile=%s",
             app_wake_profile_profile_name(wake_config.profile));

    /* Schedule transition to Phase 2 after PHASE1_DURATION_MS */
    esp_timer_stop(s_phase1_timer);
    ESP_ERROR_CHECK(esp_timer_start_once(
        s_phase1_timer,
        (uint64_t)APP_BLE_WAKE_PHASE1_DURATION_MS * 1000ULL));

    return ESP_OK;
}

bool app_ble_wakeup_is_busy(void)
{
    return s_state != BLE_STATE_IDLE;
}
