/**
 * @file ws_heartbeat.h
 * @brief WebSocket 心跳检测模块
 *
 * 负责：
 * - 定期检测客户端存活状态
 * - 发送 ping 帧检测连接
 * - 清理超时/无响应的连接
 */

#ifndef WS_HEARTBEAT_H
#define WS_HEARTBEAT_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ws_client_mgr.h"

// ==================== 配置 ====================

/**
 * @brief 心跳检测配置
 */
typedef struct {
    uint8_t check_interval_sec;     // 检测间隔（秒）
    uint16_t client_timeout_sec;    // 客户端超时时间（秒）
    uint8_t ping_timeout_sec;       // ping 响应超时（秒）
    bool auto_ping;                 // 是否自动发送 ping
} ws_heartbeat_config_t;

/**
 * @brief 心跳上下文
 */
typedef struct {
    TaskHandle_t task_handle;       // 心跳任务句柄
    bool running;                   // 运行标志
    ws_client_mgr_t *client_mgr;    // 客户端管理器引用
    httpd_handle_t http_server;     // HTTP 服务器句柄
    ws_heartbeat_config_t config;  // 配置

    // 统计
    uint32_t total_pings_sent;      // 总 ping 发送数
    uint32_t total_timeouts;         // 总超时数
    uint32_t total_pong_received;   // 总 pong 响应数
} ws_heartbeat_ctx_t;

/**
 * @brief 初始化心跳检测
 * @param ctx 心跳上下文指针
 * @param client_mgr 客户端管理器
 * @param http_server HTTP 服务器句柄
 * @param config 配置
 * @return esp_err_t
 */
esp_err_t ws_heartbeat_init(ws_heartbeat_ctx_t *ctx,
                            ws_client_mgr_t *client_mgr,
                            httpd_handle_t http_server,
                            const ws_heartbeat_config_t *config);

/**
 * @brief 启动心跳检测任务
 * @param ctx 心跳上下文指针
 * @return esp_err_t
 */
esp_err_t ws_heartbeat_start(ws_heartbeat_ctx_t *ctx);

/**
 * @brief 停止心跳检测任务
 * @param ctx 心跳上下文指针
 * @return esp_err_t
 */
esp_err_t ws_heartbeat_stop(ws_heartbeat_ctx_t *ctx);

/**
 * @brief 销毁心跳检测
 * @param ctx 心跳上下文指针
 */
void ws_heartbeat_deinit(ws_heartbeat_ctx_t *ctx);

/**
 * @brief 发送 ping 到指定客户端
 * @param http_server HTTP 服务器句柄
 * @param fd socket 文件描述符
 * @return esp_err_t
 */
esp_err_t ws_heartbeat_send_ping(httpd_handle_t http_server, int fd);

/**
 * @brief 获取心跳统计
 * @param ctx 心跳上下文指针
 * @param pings_sent 总 ping 发送数
 * @param timeouts 总超时数
 * @param pongs_received 总 pong 响应数
 */
void ws_heartbeat_get_stats(const ws_heartbeat_ctx_t *ctx,
                           uint32_t *pings_sent, uint32_t *timeouts, uint32_t *pongs_received);

#endif // WS_HEARTBEAT_H
