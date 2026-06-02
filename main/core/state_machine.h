#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include "esp_err.h"

typedef enum {
    STATE_INIT = 0,
    STATE_SOFTAP_CONFIG,    // SoftAP 配置模式
    STATE_WIFI_CONNECTING,  // WiFi 连接中
    STATE_WIFI_CONNECTED,   // WiFi 已连接
    STATE_MQTT_CONNECTING,  // MQTT 连接中
    STATE_MQTT_CONNECTED,   // MQTT 已连接
    STATE_RUNNING,          // 正常运行
    STATE_ERROR,            // 错误状态
} app_state_t;

typedef struct {
    app_state_t state;
    uint32_t state_enter_time;
    uint32_t retry_count;
} state_machine_t;

esp_err_t state_machine_init(void);
app_state_t state_machine_get_state(void);
void state_machine_transition(app_state_t new_state);
void state_machine_run(void);

#endif
