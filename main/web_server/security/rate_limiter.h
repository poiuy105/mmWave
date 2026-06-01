/**
 * @file rate_limiter.h
 * @brief 请求速率限制器
 *
 * 基于滑动窗口算法的速率限制实现
 */

#ifndef RATE_LIMITER_H
#define RATE_LIMITER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// ==================== 配置 ====================

/**
 * @brief 速率限制器配置
 */
typedef struct {
    uint16_t max_requests;          // 窗口内最大请求数
    uint16_t window_ms;            // 时间窗口（毫秒）
    uint16_t block_duration_sec;   // 超限后阻塞时间（秒）
    uint16_t max_entries;           // 最大 IP 条目数
} rate_limiter_config_t;

// ==================== API ====================

/**
 * @brief 初始化速率限制器
 * @param config 配置
 * @return esp_err_t
 */
esp_err_t rate_limiter_init(const rate_limiter_config_t *config);

/**
 * @brief 销毁速率限制器
 */
void rate_limiter_deinit(void);

/**
 * @brief 检查请求是否允许
 * @param client_ip 客户端 IP 地址
 * @return true 允许
 * @return false 拒绝（超限或被阻塞）
 */
bool rate_limiter_check(const char *client_ip);

/**
 * @brief 重置指定 IP 的计数
 * @param client_ip 客户端 IP 地址
 */
void rate_limiter_reset(const char *client_ip);

/**
 * @brief 获取统计信息
 * @param total_hits 总检查数
 * @param blocked_hits 被阻止的请求数
 * @param active_entries 当前活跃条目数
 */
void rate_limiter_get_stats(uint32_t *total_hits, uint32_t *blocked_hits, uint16_t *active_entries);

/**
 * @brief 检查是否已初始化
 */
bool rate_limiter_is_initialized(void);

/**
 * @brief 获取默认配置
 */
const rate_limiter_config_t* rate_limiter_get_default_config(void);

#endif // RATE_LIMITER_H
