#include "app_mdns.h"

#include <string.h>

#include "esp_log.h"
#include "mdns.h"

#include "app_status.h"

static const char *TAG = "mdns";

esp_err_t app_mdns_start(void)
{
    char id[8] = {0};
    app_status_device_id(id, sizeof(id));

    char hostname[32];
    snprintf(hostname, sizeof(hostname), "dangbei-wol-%s", id);

    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(hostname));
    ESP_ERROR_CHECK(mdns_instance_name_set("Dangbei WOL"));

    mdns_txt_item_t txt[] = {
        {"id", id},
        {"fw", APP_FW_VERSION},
        {"chip", APP_CHIP_NAME},
    };

    esp_err_t err = mdns_service_add(NULL, APP_MDNS_SERVICE_TYPE,
                                     APP_MDNS_PROTOCOL, APP_MDNS_PORT,
                                     txt, sizeof(txt) / sizeof(txt[0]));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_service_add failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "mDNS started, hostname=%s.local", hostname);
    return ESP_OK;
}
