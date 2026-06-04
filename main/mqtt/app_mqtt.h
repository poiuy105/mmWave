#ifndef APP_MQTT_H
#define APP_MQTT_H

#include "esp_err.h"
#include "radar_adapter/radar_adapter.h"

// MQTT 状态
typedef enum {
    MQTT_STATE_DISCONNECTED = 0,
    MQTT_STATE_CONNECTING,
    MQTT_STATE_CONNECTED,
    MQTT_STATE_ERROR,
} mqtt_state_t;

// MQTT 配置
typedef struct {
    char uri[128];
    uint16_t port;
    char username[64];
    char password[64];
} mqtt_config_t;

// ============ 初始化和连接 ============
esp_err_t app_mqtt_init(void);
esp_err_t app_mqtt_connect(const mqtt_config_t *config);
esp_err_t app_mqtt_disconnect(void);
void app_mqtt_deinit(void);

// ============ 状态查询 ============
mqtt_state_t app_mqtt_get_state(void);
bool app_mqtt_is_connected(void);

// ============ 暂停/恢复（上传等内存密集操作时使用） ============
void app_mqtt_pause(void);
void app_mqtt_resume(void);

// ============ 发布接口 ============
esp_err_t app_mqtt_publish(const char *topic, const char *data, int len, int qos, bool retain);

// ============ 雷达数据发布 ============
esp_err_t app_mqtt_publish_radar_frame(const radar_frame_t *frame);
esp_err_t app_mqtt_publish_system_info(void);
esp_err_t app_mqtt_publish_status(const char *status);

// ============ Home Assistant 发现 ============
esp_err_t app_mqtt_publish_ha_discovery(void);

// ============ LED 控制 ============
esp_err_t app_mqtt_publish_led_state(bool on);

// ============ 区域状态上报 ============
esp_err_t app_mqtt_publish_zone_status(void);

// ============ 动态 Zone Discovery ============
esp_err_t app_mqtt_publish_zone_discovery(uint8_t zone_id);
esp_err_t app_mqtt_remove_zone_discovery(uint8_t zone_id);

#endif