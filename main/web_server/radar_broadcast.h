/**
 * @file radar_broadcast.h
 * @brief 雷达数据广播模块 - 定时将雷达数据推送到 WebSocket 客户端
 */

#ifndef RADAR_BROADCAST_H
#define RADAR_BROADCAST_H

#include "esp_err.h"
#include "core/server_context.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动雷达数据广播任务
 * @param ctx 服务器上下文（包含 ws_server 和 config）
 * @return ESP_OK on success
 */
esp_err_t radar_broadcast_start(server_context_t *ctx);

/**
 * @brief 停止雷达数据广播任务
 * @return ESP_OK on success
 */
esp_err_t radar_broadcast_stop(void);

/**
 * @brief 检查广播任务是否运行中
 * @return true if running
 */
bool radar_broadcast_is_running(void);

#ifdef __cplusplus
}
#endif

#endif // RADAR_BROADCAST_H
