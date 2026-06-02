#ifndef DEFAULT_CONFIG_H
#define DEFAULT_CONFIG_H

#include "esp_system.h"

// WiFi 默认值（空，需要用户配置）
#define DEFAULT_WIFI_SSID ""
#define DEFAULT_WIFI_PASSWORD ""

// MQTT 默认值
#define DEFAULT_MQTT_URI "mqtt://test.mosquitto.org"
#define DEFAULT_MQTT_PORT 1883
#define DEFAULT_MQTT_USER ""
#define DEFAULT_MQTT_PASS ""

// 首次启动标志
#define DEFAULT_FIRST_BOOT true

#endif