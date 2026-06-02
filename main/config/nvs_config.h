#ifndef NVS_CONFIG_H
#define NVS_CONFIG_H

#include "esp_err.h"
#include "default_config.h"

// NVS 命名空间
#define NVS_NAMESPACE "app_config"

// NVS Key 定义
#define NVS_KEY_WIFI_SSID "wifi_ssid"
#define NVS_KEY_WIFI_PASSWORD "wifi_pass"
#define NVS_KEY_MQTT_URI "mqtt_uri"
#define NVS_KEY_MQTT_PORT "mqtt_port"
#define NVS_KEY_MQTT_USER "mqtt_user"
#define NVS_KEY_MQTT_PASS "mqtt_pass"
#define NVS_KEY_FIRST_BOOT "first_boot"

/**
 * @brief 应用配置结构体（WiFi + MQTT）
 */
typedef struct {
    bool is_configured;           /*!< 配置是否有效 */
    char wifi_ssid[32];           /*!< WiFi SSID */
    char wifi_password[64];       /*!< WiFi 密码 */
    char mqtt_uri[128];           /*!< MQTT broker URI */
    uint16_t mqtt_port;           /*!< MQTT 端口 */
    char mqtt_username[64];       /*!< MQTT 用户名 */
    char mqtt_password[64];       /*!< MQTT 密码 */
    bool first_boot;              /*!< 首次启动标志 */
} app_config_t;

// ============ NVS 初始化 ============
esp_err_t nvs_init_config(void);

// ============ WiFi 配置 ============
esp_err_t nvs_save_wifi_config(const char *ssid, const char *password);
esp_err_t nvs_read_wifi_config(char *ssid, size_t ssid_size, char *password, size_t pass_size);

// ============ MQTT 配置 ============
esp_err_t nvs_save_mqtt_config(const char *uri, uint16_t port, const char *user, const char *pass);
esp_err_t nvs_read_mqtt_config(char *uri, size_t uri_size, uint16_t *port, 
                               char *user, size_t user_size, char *pass, size_t pass_size);

// ============ 配置管理 ============
esp_err_t nvs_reset_config(void);
esp_err_t nvs_reset_network_config(void);
esp_err_t nvs_load_all_config(app_config_t *config);
bool nvs_is_config_valid(app_config_t *config);

// ============ 首次启动标志 ============
esp_err_t nvs_set_first_boot(bool first_boot);
bool nvs_get_first_boot(void);

#endif