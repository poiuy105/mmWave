#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_wifi_types.h"

#define DEFAULT_SOFTAP_CHANNEL 1

typedef struct {
    char ssid[32];
    char password[64];
    bool sta_connected;
    int8_t rssi;
} wifi_manager_config_t;

esp_err_t wifi_manager_init(void);

esp_err_t wifi_manager_connect_sta(const char *ssid, const char *password);

esp_err_t wifi_manager_stop_sta(void);

bool wifi_manager_is_sta_connected(void);

int8_t wifi_manager_get_rssi(void);

esp_err_t wifi_manager_start_softap(const char *ssid, const char *password);

esp_err_t wifi_manager_stop_softap(void);

#define WIFI_SCAN_MAX_RESULTS 20

typedef struct {
    char ssid[32];
    uint8_t bssid[6];
    int8_t rssi;
    uint8_t channel;
    wifi_auth_mode_t authmode;
} wifi_scan_result_t;

typedef struct {
    wifi_scan_result_t aps[WIFI_SCAN_MAX_RESULTS];
    uint16_t count;
} wifi_scan_results_t;

esp_err_t wifi_manager_scan_wifi(wifi_scan_results_t *results);

#endif // WIFI_MANAGER_H
