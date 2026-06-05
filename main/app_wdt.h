#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// 看门狗任务 ID 枚举
// 每个独立任务应有独立 ID，避免 unregister 影响其他任务
typedef enum {
    WDT_TASK_APP,              // 主状态机任务
    WDT_TASK_DNS,              // DNS 服务器任务
    WDT_TASK_WS_HEARTBEAT,     // WebSocket 心跳任务
    WDT_TASK_RADAR_BROADCAST,  // 雷达广播任务
    WDT_TASK_RADAR_LD2450,     // LD2450 雷达解析任务
    WDT_TASK_RADAR_LD2452,     // LD2452 雷达解析任务
    WDT_TASK_RADAR_LD2460,     // LD2460 雷达解析任务
    WDT_TASK_RADAR_LD2461,     // LD2461 雷达解析任务
    WDT_TASK_RADAR_LD6002B,    // LD6002B 雷达解析任务
    WDT_TASK_RADAR_LD6004,     // LD6004 雷达解析任务
    WDT_TASK_RADAR_R60ABD1,    // R60ABD1 雷达解析任务
    WDT_TASK_COUNT             // 任务总数
} wdt_task_id_t;

/**
 * @brief 初始化看门狗模块
 */
esp_err_t app_wdt_init(void);

/**
 * @brief 注册当前任务到看门狗（在任务入口处调用）
 * @param id  任务 ID
 */
esp_err_t app_wdt_register_task(wdt_task_id_t id);

/**
 * @brief 注销任务（任务退出前调用）
 * @param id  任务 ID
 */
esp_err_t app_wdt_unregister_task(wdt_task_id_t id);

/**
 * @brief 喂狗（在任务主循环中调用）
 * @param id  任务 ID
 */
void app_wdt_feed(wdt_task_id_t id);

/**
 * @brief 获取任务健康状态
 * @param id  任务 ID
 * @return true 如果任务在超时时间内喂过狗，false 如果超时或未注册
 */
bool app_wdt_is_healthy(wdt_task_id_t id);

/**
 * @brief 获取指定任务的最后喂狗时间（秒，uptime）
 * @param id  任务 ID
 * @return 最后喂狗时间，0 表示从未喂狗
 */
uint32_t app_wdt_get_last_feed_time(wdt_task_id_t id);

/**
 * @brief 获取看门狗超时时间（秒）
 */
uint32_t app_wdt_get_timeout_sec(void);
