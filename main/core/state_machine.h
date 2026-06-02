#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include "esp_err.h"

// ============ 状态定义 ============
typedef enum {
    STATE_INIT = 0,          // 初始化
    STATE_SOFTAP,            // AP 热点模式，等待用户配置
    STATE_CONFIG,            // 配置已保存，正在连接 WiFi
    STATE_CONNECTING,        // WiFi 已连接，正在连接 MQTT
    STATE_RUNNING,          // MQTT 已连接，正常运行
    STATE_ERROR             // 错误状态
} app_state_t;

// ============ 事件定义 ============
typedef enum {
    EVENT_INIT_COMPLETE = 0,     // 初始化完成
    EVENT_CONFIG_RECEIVED,      // 收到配置（Web/API）
    EVENT_WIFI_CONNECTED,       // WiFi 连接成功
    EVENT_WIFI_DISCONNECTED,   // WiFi 断开
    EVENT_MQTT_CONNECTED,       // MQTT 连接成功
    EVENT_MQTT_DISCONNECTED,   // MQTT 断开
    EVENT_RESET_CONFIG,         // 重置配置
    EVENT_TIMEOUT               // 超时
} app_event_t;

// ============ 接口 ============
esp_err_t state_machine_init(void);
app_state_t state_machine_get_state(void);
const char* state_machine_get_state_name(app_state_t state);
void state_machine_trigger_event(app_event_t event);
void state_machine_reset(void);

#endif
