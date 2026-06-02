#include "system_info.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "system_info";

static uint32_t s_boot_time = 0;

void system_info_init(void)
{
    s_boot_time = (uint32_t)(esp_timer_get_time() / 1000000);  // 秒
    ESP_LOGI(TAG, "System info initialized, boot_time=%lu", s_boot_time);
}

void system_info_get(system_info_t *info)
{
    if (info == NULL) return;

    // 堆内存信息
    info->free_heap = esp_get_free_heap_size();
    info->min_free_heap = esp_get_minimum_free_heap_size();

    // 运行时间
    uint32_t current_time = (uint32_t)(esp_timer_get_time() / 1000000);
    info->uptime_seconds = (current_time > s_boot_time) ? (current_time - s_boot_time) : 0;

    // WiFi 信息
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        info->wifi_connected = true;
        info->wifi_rssi = ap_info.rssi;
        info->wifi_channel = ap_info.primary;
    } else {
        info->wifi_connected = false;
        info->wifi_rssi = 0;
        info->wifi_channel = 0;
    }

    // IP 地址
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif != NULL && info->wifi_connected) {
        esp_netif_ip_info_t ip_info;
        esp_netif_get_ip_info(netif, &ip_info);
        snprintf(info->wifi_ip, sizeof(info->wifi_ip), IPSTR, IP2STR(&ip_info.ip));
    } else {
        snprintf(info->wifi_ip, sizeof(info->wifi_ip), "0.0.0.0");
    }
}

char* system_info_to_json(const system_info_t *info)
{
    cJSON *root = cJSON_CreateObject();
    
    cJSON_AddNumberToObject(root, "free_heap", info->free_heap);
    cJSON_AddNumberToObject(root, "min_free_heap", info->min_free_heap);
    cJSON_AddNumberToObject(root, "uptime_seconds", info->uptime_seconds);
    cJSON_AddNumberToObject(root, "wifi_rssi", info->wifi_rssi);
    cJSON_AddStringToObject(root, "wifi_ip", info->wifi_ip);
    cJSON_AddBoolToObject(root, "wifi_connected", info->wifi_connected);
    cJSON_AddNumberToObject(root, "wifi_channel", info->wifi_channel);
    
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}
