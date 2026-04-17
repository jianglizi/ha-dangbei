#include "app_captive_dns.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"

#include "app_wifi.h"

static const char *TAG = "captive_dns";

#define DNS_PORT 53
#define DNS_PACKET_MAX_LEN 512

static TaskHandle_t s_dns_task;

typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header_t;

static size_t dns_question_end(const uint8_t *packet, size_t len)
{
    size_t pos = sizeof(dns_header_t);
    while (pos < len && packet[pos] != 0) {
        uint8_t label_len = packet[pos];
        pos += (size_t)label_len + 1;
    }
    if (pos + 5 > len) {
        return 0;
    }
    return pos + 5;
}

static size_t build_dns_response(
    const uint8_t *request,
    size_t request_len,
    uint8_t *response,
    size_t response_len,
    const esp_ip4_addr_t *ip
)
{
    if (request_len < sizeof(dns_header_t) || response_len < sizeof(dns_header_t)) {
        return 0;
    }

    const dns_header_t *req_hdr = (const dns_header_t *)request;
    if (ntohs(req_hdr->qdcount) == 0) {
        return 0;
    }

    size_t question_end = dns_question_end(request, request_len);
    if (question_end == 0) {
        return 0;
    }

    size_t required = question_end + 16;
    if (required > response_len) {
        return 0;
    }

    memcpy(response, request, question_end);

    dns_header_t *resp_hdr = (dns_header_t *)response;
    resp_hdr->flags = htons(0x8180);
    resp_hdr->qdcount = htons(1);
    resp_hdr->ancount = htons(1);
    resp_hdr->nscount = 0;
    resp_hdr->arcount = 0;

    size_t pos = question_end;
    response[pos++] = 0xC0;
    response[pos++] = 0x0C;
    response[pos++] = 0x00;
    response[pos++] = 0x01;
    response[pos++] = 0x00;
    response[pos++] = 0x01;
    response[pos++] = 0x00;
    response[pos++] = 0x00;
    response[pos++] = 0x00;
    response[pos++] = 0x3C;
    response[pos++] = 0x00;
    response[pos++] = 0x04;
    response[pos++] = esp_ip4_addr1(ip);
    response[pos++] = esp_ip4_addr2(ip);
    response[pos++] = esp_ip4_addr3(ip);
    response[pos++] = esp_ip4_addr4(ip);
    return pos;
}

static void dns_task(void *arg)
{
    (void)arg;

    esp_netif_ip_info_t ip_info;
    esp_netif_t *ap_netif = app_wifi_ap_netif();
    if (ap_netif == NULL || esp_netif_get_ip_info(ap_netif, &ip_info) != ESP_OK) {
        ESP_LOGE(TAG, "cannot resolve SoftAP IP");
        vTaskDelete(NULL);
        return;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket failed: errno=%d", errno);
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "bind failed: errno=%d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "captive DNS started on port %d", DNS_PORT);

    uint8_t request[DNS_PACKET_MAX_LEN];
    uint8_t response[DNS_PACKET_MAX_LEN];

    while (true) {
        struct sockaddr_in source_addr = {0};
        socklen_t socklen = sizeof(source_addr);
        int len = recvfrom(
            sock,
            request,
            sizeof(request),
            0,
            (struct sockaddr *)&source_addr,
            &socklen
        );
        if (len <= 0) {
            continue;
        }

        size_t resp_len = build_dns_response(
            request,
            (size_t)len,
            response,
            sizeof(response),
            &ip_info.ip
        );
        if (resp_len == 0) {
            continue;
        }

        sendto(
            sock,
            response,
            resp_len,
            0,
            (struct sockaddr *)&source_addr,
            sizeof(source_addr)
        );
    }
}

esp_err_t app_captive_dns_start(void)
{
    if (s_dns_task != NULL) {
        return ESP_OK;
    }

    BaseType_t created = xTaskCreate(
        dns_task,
        "captive_dns",
        4096,
        NULL,
        tskIDLE_PRIORITY + 1,
        &s_dns_task
    );
    return created == pdPASS ? ESP_OK : ESP_FAIL;
}
