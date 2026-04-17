#include "app_status.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "esp_mac.h"
#include "esp_timer.h"

static _Atomic uint32_t s_wake_count;
static _Atomic int64_t s_last_wake_us;
static _Atomic bool s_ble_busy;

void app_status_init(void)
{
    atomic_store(&s_wake_count, 0);
    atomic_store(&s_last_wake_us, 0);
    atomic_store(&s_ble_busy, false);
}

void app_status_record_wake(void)
{
    atomic_fetch_add(&s_wake_count, 1);
    atomic_store(&s_last_wake_us, esp_timer_get_time());
}

uint32_t app_status_wake_count(void)
{
    return atomic_load(&s_wake_count);
}

int64_t app_status_last_wake_us(void)
{
    return atomic_load(&s_last_wake_us);
}

void app_status_set_ble_busy(bool busy)
{
    atomic_store(&s_ble_busy, busy);
}

bool app_status_ble_busy(void)
{
    return atomic_load(&s_ble_busy);
}

void app_status_device_id(char *out, size_t len)
{
    if (out == NULL || len < 7) {
        if (out != NULL && len > 0) {
            out[0] = '\0';
        }
        return;
    }
    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        snprintf(out, len, "000000");
        return;
    }
    snprintf(out, len, "%02x%02x%02x", mac[3], mac[4], mac[5]);
}
