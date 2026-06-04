#include "dns_server.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_wdt.h"
#include <string.h>

static const char *TAG = "dns_server";

#define DNS_PORT 53
#define DNS_MAX_LEN 256
#define DNS_TTL 300

typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header_t;

static TaskHandle_t dns_task_handle = NULL;
static int dns_socket = -1;
static uint32_t dns_override_ip = 0;

static void dns_server_task(void *pvParameters)
{
    uint8_t rx_buffer[DNS_MAX_LEN];
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    while (dns_socket >= 0) {
        int len = recvfrom(dns_socket, rx_buffer, sizeof(rx_buffer), 0,
                          (struct sockaddr *)&client_addr, &addr_len);
        if (len < 0) {
            if (dns_socket >= 0) {
                ESP_LOGE(TAG, "recvfrom failed");
            }
            break;
        }

        if (len < sizeof(dns_header_t)) {
            continue;
        }

        dns_header_t *header = (dns_header_t *)rx_buffer;
        uint16_t flags = ntohs(header->flags);
        uint16_t qdcount = ntohs(header->qdcount);

        if ((flags & 0x8000) != 0 || qdcount == 0) {
            continue;
        }

        uint8_t tx_buffer[DNS_MAX_LEN];
        memcpy(tx_buffer, rx_buffer, len);

        dns_header_t *resp_header = (dns_header_t *)tx_buffer;
        resp_header->flags = htons(0x8180);
        resp_header->ancount = htons(1);
        resp_header->nscount = 0;
        resp_header->arcount = 0;

        int resp_len = len;

        tx_buffer[resp_len++] = 0xC0;
        tx_buffer[resp_len++] = 0x0C;
        tx_buffer[resp_len++] = 0x00;
        tx_buffer[resp_len++] = 0x01;
        tx_buffer[resp_len++] = 0x00;
        tx_buffer[resp_len++] = 0x01;
        tx_buffer[resp_len++] = 0x00;
        tx_buffer[resp_len++] = 0x00;
        tx_buffer[resp_len++] = 0x01;
        tx_buffer[resp_len++] = 0x2C;
        tx_buffer[resp_len++] = 0x00;
        tx_buffer[resp_len++] = 0x04;
        memcpy(&tx_buffer[resp_len], &dns_override_ip, 4);
        resp_len += 4;

        sendto(dns_socket, tx_buffer, resp_len, 0,
               (struct sockaddr *)&client_addr, addr_len);
        app_wdt_feed(WDT_TASK_DNS);
    }

    dns_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t dns_server_start(uint16_t port, const char *override_ip)
{
    if (dns_task_handle != NULL) {
        ESP_LOGW(TAG, "DNS server already running");
        return ESP_OK;
    }

    dns_override_ip = inet_addr(override_ip);
    if (dns_override_ip == INADDR_NONE) {
        ESP_LOGE(TAG, "Invalid IP address");
        return ESP_ERR_INVALID_ARG;
    }

    dns_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (dns_socket < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        return ESP_FAIL;
    }

    int opt = 1;
    setsockopt(dns_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = INADDR_ANY
    };

    if (bind(dns_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind socket");
        close(dns_socket);
        dns_socket = -1;
        return ESP_FAIL;
    }

    if (xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 5, &dns_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task");
        close(dns_socket);
        dns_socket = -1;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "DNS server started on port %d, override IP: %s", port, override_ip);
    return ESP_OK;
}

esp_err_t dns_server_stop(void)
{
    if (dns_task_handle == NULL) {
        return ESP_OK;
    }

    int sock = dns_socket;
    dns_socket = -1;

    if (sock >= 0) {
        close(sock);
    }

    while (dns_task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "DNS server stopped");
    return ESP_OK;
}
