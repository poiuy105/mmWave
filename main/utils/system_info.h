#ifndef SYSTEM_INFO_H
#define SYSTEM_INFO_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t free_heap;
    uint32_t min_free_heap;
    uint32_t uptime_seconds;
    int8_t wifi_rssi;
    char wifi_ip[16];
    bool wifi_connected;
    uint8_t wifi_channel;
} system_info_t;

/**
 * @brief 初始化系统信息模块
 */
void system_info_init(void);

/**
 * @brief 获取系统信息
 */
void system_info_get(system_info_t *info);

/**
 * @brief 格式化系统信息为JSON字符串
 */
char* system_info_to_json(const system_info_t *info);

#endif
