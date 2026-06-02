#include <stdlib.h>
#include <string.h>
#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "softap.h"

static const char *TAG = "wifi_manager";
static wifi_manager_config_t current_config = {0};

// Static netif handles for proper lifecycle management
static esp_netif_t *sta_netif = NULL;
static esp_netif_t *ap_netif = NULL;

esp_err_t wifi_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing WiFi manager");
    return ESP_OK;
}

esp_err_t wifi_manager_connect_sta(const char *ssid, const char *password)
{
    ESP_LOGI(TAG, "Connecting to WiFi station: %s", ssid);
    
    // Destroy existing netifs to avoid conflicts
    if (sta_netif != NULL) {
        ESP_LOGI(TAG, "Destroying existing STA netif");
        esp_netif_destroy(sta_netif);
        sta_netif = NULL;
    }
    if (ap_netif != NULL) {
        ESP_LOGI(TAG, "Destroying existing AP netif");
        esp_netif_destroy(ap_netif);
        ap_netif = NULL;
    }
    
    sta_netif = esp_netif_create_default_wifi_sta();
    if (sta_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create STA netif");
        return ESP_FAIL;
    }
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password) - 1);
    
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi set mode failed: %s", esp_err_to_name(ret));
        esp_wifi_deinit();
        return ret;
    }
    
    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi set config failed: %s", esp_err_to_name(ret));
        esp_wifi_deinit();
        return ret;
    }
    
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(ret));
        esp_wifi_deinit();
        return ret;
    }
    
    strncpy(current_config.ssid, ssid, sizeof(current_config.ssid) - 1);
    strncpy(current_config.password, password, sizeof(current_config.password) - 1);
    
    return ESP_OK;
}

esp_err_t wifi_manager_stop_sta(void)
{
    ESP_LOGI(TAG, "Stopping WiFi station");
    
    esp_wifi_stop();
    esp_wifi_deinit();
    
    // Destroy STA netif
    if (sta_netif != NULL) {
        ESP_LOGI(TAG, "Destroying STA netif");
        esp_netif_destroy(sta_netif);
        sta_netif = NULL;
    }
    
    current_config.sta_connected = false;
    
    return ESP_OK;
}

bool wifi_manager_is_sta_connected(void)
{
    return current_config.sta_connected;
}

int8_t wifi_manager_get_rssi(void)
{
    return current_config.rssi;
}

void wifi_manager_set_sta_connected(bool connected, int8_t rssi)
{
    current_config.sta_connected = connected;
    current_config.rssi = rssi;
}

esp_err_t wifi_manager_start_softap(const char *ssid, const char *password)
{
    ESP_LOGI(TAG, "Starting WiFi SoftAP: %s", ssid);
    
    // Destroy existing netifs to avoid conflicts
    if (sta_netif != NULL) {
        ESP_LOGI(TAG, "Destroying existing STA netif before starting SoftAP");
        esp_netif_destroy(sta_netif);
        sta_netif = NULL;
    }
    if (ap_netif != NULL) {
        ESP_LOGI(TAG, "Destroying existing AP netif before starting SoftAP");
        esp_netif_destroy(ap_netif);
        ap_netif = NULL;
    }
    
    ap_netif = esp_netif_create_default_wifi_ap();
    if (ap_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create AP netif");
        return ESP_FAIL;
    }
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.ap.ssid, ssid, sizeof(wifi_cfg.ap.ssid) - 1);
    wifi_cfg.ap.ssid_len = strlen(ssid);
    wifi_cfg.ap.channel = DEFAULT_SOFTAP_CHANNEL;
    wifi_cfg.ap.max_connection = 4;
    wifi_cfg.ap.beacon_interval = 100;
    
    // Handle empty password (open network)
    if (password == NULL || strlen(password) == 0) {
        wifi_cfg.ap.authmode = WIFI_AUTH_OPEN;
        ESP_LOGI(TAG, "SoftAP configured as open network (no password)");
    } else {
        strncpy((char *)wifi_cfg.ap.password, password, sizeof(wifi_cfg.ap.password) - 1);
        wifi_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }
    
    ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi set mode failed: %s", esp_err_to_name(ret));
        esp_wifi_deinit();
        return ret;
    }
    
    ret = esp_wifi_set_config(WIFI_IF_AP, &wifi_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi set config failed: %s", esp_err_to_name(ret));
        esp_wifi_deinit();
        return ret;
    }
    
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(ret));
        esp_wifi_deinit();
        return ret;
    }
    
    ESP_LOGI(TAG, "SoftAP started successfully");
    ESP_LOGI(TAG, "SoftAP IP: 192.168.4.1");
    
    return ESP_OK;
}

esp_err_t wifi_manager_stop_softap(void)
{
    // Check if WiFi driver is initialized before stopping
    wifi_mode_t mode;
    esp_err_t ret = esp_wifi_get_mode(&mode);
    if (ret == ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGI(TAG, "WiFi driver not initialized, skip stopping SoftAP");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Stopping WiFi SoftAP");
    
    esp_wifi_stop();
    esp_wifi_deinit();
    
    // Destroy AP netif
    if (ap_netif != NULL) {
        ESP_LOGI(TAG, "Destroying AP netif");
        esp_netif_destroy(ap_netif);
        ap_netif = NULL;
    }
    
    return ESP_OK;
}

esp_err_t wifi_manager_scan_wifi(wifi_scan_results_t *results)
{
    if (results == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(results, 0, sizeof(wifi_scan_results_t));
    
    // Start scan
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };
    
    esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Get scan results
    uint16_t ap_count = WIFI_SCAN_MAX_RESULTS;
    wifi_ap_record_t ap_records[WIFI_SCAN_MAX_RESULTS];
    
    ret = esp_wifi_scan_get_ap_records(&ap_count, ap_records);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get scan results: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Copy results
    results->count = ap_count;
    for (int i = 0; i < ap_count && i < WIFI_SCAN_MAX_RESULTS; i++) {
        strncpy(results->aps[i].ssid, (char *)ap_records[i].ssid, sizeof(results->aps[i].ssid) - 1);
        memcpy(results->aps[i].bssid, ap_records[i].bssid, 6);
        results->aps[i].rssi = ap_records[i].rssi;
        results->aps[i].channel = ap_records[i].primary;
        results->aps[i].authmode = ap_records[i].authmode;
    }
    
    ESP_LOGI(TAG, "WiFi scan found %d APs", results->count);
    return ESP_OK;
}
