#include <string.h>
#include <stdio.h>
#include "softap.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_netif.h"

static const char *TAG = "softap";

esp_err_t softap_generate_ssid_with_mac(char *ssid_out, size_t ssid_size)
{
    uint8_t mac[6];
    esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (ret != ESP_OK) {
        // Fallback to WiFi MAC
        ret = esp_wifi_get_mac(WIFI_IF_AP, mac);
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get MAC address");
        snprintf(ssid_out, ssid_size, "%s", DEFAULT_SOFTAP_SSID);
        return ret;
    }
    
    // Format: LD-Radar-AP-XXXX (last 4 hex digits of MAC)
    snprintf(ssid_out, ssid_size, "%s-%02X%02X", 
             DEFAULT_SOFTAP_SSID, mac[4], mac[5]);
    
    ESP_LOGI(TAG, "Generated SoftAP SSID: %s", ssid_out);
    return ESP_OK;
}
