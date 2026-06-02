#include <string.h>
#include "nvs_config.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "nvs_config";

esp_err_t nvs_init_config(void)
{
    ESP_LOGI(TAG, "Initializing NVS flash...");
    
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS init failed (0x%x), erasing partition and retrying...", err);
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "NVS flash erase failed: %s", esp_err_to_name(err));
            return err;
        }
        err = nvs_flash_init();
    }
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS flash init failed: 0x%x (%s)", err, esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "NVS flash initialized successfully");
        
        // Log NVS statistics for debugging
        nvs_stats_t nvs_stats;
        err = nvs_get_stats(NULL, &nvs_stats);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "NVS: used_entries=%d, free_entries=%d, total_entries=%d",
                     nvs_stats.used_entries, nvs_stats.free_entries, 
                     nvs_stats.used_entries + nvs_stats.free_entries);
        }
    }
    
    return err;
}

esp_err_t nvs_save_wifi_config(const char *ssid, const char *password)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(handle, NVS_KEY_WIFI_SSID, ssid);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }
    
    err = nvs_set_str(handle, NVS_KEY_WIFI_PASSWORD, password);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    
    ESP_LOGI(TAG, "WiFi config saved successfully (SSID: %s)", ssid);
    return err;
}

esp_err_t nvs_read_wifi_config(char *ssid, size_t ssid_size, char *password, size_t pass_size)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;

    memset(ssid, 0, ssid_size);
    memset(password, 0, pass_size);
    
    err = nvs_get_str(handle, NVS_KEY_WIFI_SSID, ssid, &ssid_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return err;
    }
    
    err = nvs_get_str(handle, NVS_KEY_WIFI_PASSWORD, password, &pass_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return err;
    }
    
    nvs_close(handle);
    
    ESP_LOGI(TAG, "WiFi config read: SSID=%s", ssid);
    return ESP_OK;
}

esp_err_t nvs_save_mqtt_config(const char *uri, uint16_t port, const char *user, const char *pass)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    // Auto-add mqtt:// prefix if not present
    char full_uri[140] = {0};
    if (uri && strlen(uri) > 0) {
        if (strncmp(uri, "mqtt://", 7) == 0 ||
            strncmp(uri, "mqtts://", 8) == 0 ||
            strncmp(uri, "ws://", 5) == 0 ||
            strncmp(uri, "wss://", 6) == 0) {
            strncpy(full_uri, uri, sizeof(full_uri) - 1);
        } else {
            snprintf(full_uri, sizeof(full_uri), "mqtt://%s", uri);
        }
        ESP_LOGI(TAG, "MQTT URI with scheme: %s", full_uri);
    }
    
    err = nvs_set_str(handle, NVS_KEY_MQTT_URI, full_uri);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }
    
    err = nvs_set_u16(handle, NVS_KEY_MQTT_PORT, port);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }
    
    err = nvs_set_str(handle, NVS_KEY_MQTT_USER, user ? user : "");
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }
    
    err = nvs_set_str(handle, NVS_KEY_MQTT_PASS, pass ? pass : "");
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    
    ESP_LOGI(TAG, "MQTT config saved successfully");
    return err;
}

esp_err_t nvs_read_mqtt_config(char *uri, size_t uri_size, uint16_t *port, 
                               char *user, size_t user_size, char *pass, size_t pass_size)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;

    memset(uri, 0, uri_size);
    memset(user, 0, user_size);
    memset(pass, 0, pass_size);
    
    err = nvs_get_str(handle, NVS_KEY_MQTT_URI, uri, &uri_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return err;
    }
    
    uint16_t temp_port = DEFAULT_MQTT_PORT;
    err = nvs_get_u16(handle, NVS_KEY_MQTT_PORT, &temp_port);
    if (err == ESP_OK) *port = temp_port;
    else *port = DEFAULT_MQTT_PORT;
    
    err = nvs_get_str(handle, NVS_KEY_MQTT_USER, user, &user_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return err;
    }
    
    err = nvs_get_str(handle, NVS_KEY_MQTT_PASS, pass, &pass_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return err;
    }
    
    nvs_close(handle);
    return ESP_OK;
}

esp_err_t nvs_reset_config(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    
    err = nvs_erase_all(handle);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    
    ESP_LOGI(TAG, "Config reset successfully");
    return err;
}

esp_err_t nvs_reset_network_config(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    
    // Only erase network-related keys
    nvs_erase_key(handle, NVS_KEY_WIFI_SSID);
    nvs_erase_key(handle, NVS_KEY_WIFI_PASSWORD);
    nvs_erase_key(handle, NVS_KEY_MQTT_URI);
    nvs_erase_key(handle, NVS_KEY_MQTT_PORT);
    nvs_erase_key(handle, NVS_KEY_MQTT_USER);
    nvs_erase_key(handle, NVS_KEY_MQTT_PASS);
    nvs_erase_key(handle, NVS_KEY_FIRST_BOOT);
    
    err = nvs_commit(handle);
    nvs_close(handle);
    
    ESP_LOGI(TAG, "Network config reset successfully");
    return err;
}

esp_err_t nvs_set_first_boot(bool first_boot)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    
    err = nvs_set_u8(handle, NVS_KEY_FIRST_BOOT, first_boot ? 1 : 0);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    
    ESP_LOGI(TAG, "First boot flag set: %d", first_boot);
    return err;
}

bool nvs_get_first_boot(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return true;
    
    uint8_t first_boot = 1;
    err = nvs_get_u8(handle, NVS_KEY_FIRST_BOOT, &first_boot);
    
    nvs_close(handle);
    
    return (first_boot != 0);
}

esp_err_t nvs_load_all_config(app_config_t *config)
{
    memset(config, 0, sizeof(app_config_t));
    
    esp_err_t err = nvs_read_wifi_config(config->wifi_ssid, sizeof(config->wifi_ssid), 
                                         config->wifi_password, sizeof(config->wifi_password));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read WiFi config (0x%x), using defaults", err);
        strncpy(config->wifi_ssid, DEFAULT_WIFI_SSID, sizeof(config->wifi_ssid) - 1);
        strncpy(config->wifi_password, DEFAULT_WIFI_PASSWORD, sizeof(config->wifi_password) - 1);
    }
    
    err = nvs_read_mqtt_config(config->mqtt_uri, sizeof(config->mqtt_uri), 
                              &config->mqtt_port, 
                              config->mqtt_username, sizeof(config->mqtt_username), 
                              config->mqtt_password, sizeof(config->mqtt_password));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read MQTT config (0x%x), using defaults", err);
        strncpy(config->mqtt_uri, DEFAULT_MQTT_URI, sizeof(config->mqtt_uri) - 1);
        config->mqtt_port = DEFAULT_MQTT_PORT;
    }
    
    config->first_boot = nvs_get_first_boot();
    config->is_configured = nvs_is_config_valid(config);
    
    ESP_LOGI(TAG, "Config loaded: SSID=%s, MQTT=%s:%d, first_boot=%d, configured=%d",
             config->wifi_ssid, config->mqtt_uri, config->mqtt_port,
             config->first_boot, config->is_configured);
    
    return ESP_OK;
}

bool nvs_is_config_valid(app_config_t *config)
{
    if (config == NULL) return false;
    
    // WiFi SSID validation (1-32 characters)
    size_t ssid_len = strlen(config->wifi_ssid);
    if (ssid_len == 0 || ssid_len > 32) {
        ESP_LOGW(TAG, "Invalid SSID length: %d", ssid_len);
        return false;
    }
    
    // MQTT URI validation (optional - can be empty if MQTT not used)
    size_t uri_len = strlen(config->mqtt_uri);
    if (uri_len > 0) {
        // Check MQTT URI format (must start with mqtt:// or mqtts://)
        if (strncmp(config->mqtt_uri, "mqtt://", 7) != 0 && 
            strncmp(config->mqtt_uri, "mqtts://", 8) != 0 &&
            strncmp(config->mqtt_uri, "ws://", 5) != 0 &&
            strncmp(config->mqtt_uri, "wss://", 6) != 0) {
            ESP_LOGW(TAG, "Invalid MQTT URI format: %s", config->mqtt_uri);
            return false;
        }
        
        // Port validation (1-65535)
        if (config->mqtt_port == 0 || config->mqtt_port > 65535) {
            ESP_LOGW(TAG, "Invalid MQTT port: %d", config->mqtt_port);
            return false;
        }
    }
    
    return true;
}