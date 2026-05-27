/**
 * @file server_context.h
 * @brief HTTP/WebSocket 服务器全局上下文结构定义
 *
 * 工业级重构 - 统一管理服务器状态、资源和配置
 */

#ifndef SERVER_CONTEXT_H
#define SERVER_CONTEXT_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// 前向声明
struct ws_server;
struct radar_broadcast;

/**
 * @brief 服务器运行统计
 */
typedef struct {
    // HTTP 统计
    uint32_t total_requests;          // 总请求数
    uint32_t active_requests;        // 当前活跃请求
    uint32_t error_requests;         // 错误请求数
    uint32_t bytes_sent;             // 发送字节数
    uint32_t bytes_received;         // 接收字节数

    // WebSocket 统计
    uint32_t ws_connections;         // WebSocket 连接总数
    uint32_t ws_disconnections;      // WebSocket 断开总数
    uint32_t ws_messages_sent;       // 发送的消息数
    uint32_t ws_messages_failed;     // 发送失败的消息数
    uint32_t ws_heartbeats_sent;     // 发送的心跳数
    uint32_t ws_timeout_disconnects; // 超时断开的连接数

    // 系统统计
    uint32_t uptime_seconds;         // 运行时间（秒）
    uint32_t free_heap_min;          // 最小空闲堆
    uint32_t free_heap_current;      // 当前空闲堆

    // 错误统计
    uint32_t rate_limit_hits;        // 速率限制触发次数
    uint32_t validation_failures;    // 输入验证失败次数
    uint32_t timeout_errors;         // 超时错误次数
    uint32_t memory_errors;          // 内存错误次数
} server_stats_t;

/**
 * @brief 服务器运行状态
 */
typedef enum {
    SERVER_STATE_UNINITIALIZED = 0,
    SERVER_STATE_INITIALIZED,
    SERVER_STATE_RUNNING,
    SERVER_STATE_GRACEFUL_SHUTDOWN,
    SERVER_STATE_STOPPED
} server_state_t;

/**
 * @brief 服务器全局上下文
 */
typedef struct {
    // 服务器句柄
    httpd_handle_t http_server;          // HTTP 服务器句柄

    // 内部模块句柄
    struct ws_server *ws_server;         // WebSocket 服务器
    struct radar_broadcast *broadcast;   // 雷达广播模块

    // 同步机制
    SemaphoreHandle_t mutex;              // 全局状态保护互斥量
    SemaphoreHandle_t stats_mutex;       // 统计更新互斥量

    // 运行状态
    server_state_t state;                // 服务器状态
    bool graceful_shutdown_pending;      // 优雅关闭标志
    uint32_t shutdown_start_time;        // 关闭开始时间

    // 配置引用
    const struct server_config *config;  // 配置指针（只读）

    // 运行统计
    server_stats_t stats;                // 服务器统计

    // 启动时间戳
    uint32_t start_time;                 // 系统启动时间
} server_context_t;

/**
 * @brief 服务器配置结构（定义在 server_config.h）
 */
struct server_config;

/**
 * @brief 初始化服务器上下文
 * @param config 配置指针
 * @return esp_err_t
 */
esp_err_t server_context_init(const struct server_config *config);

/**
 * @brief 销毁服务器上下文
 * @return esp_err_t
 */
esp_err_t server_context_deinit(void);

/**
 * @brief 获取服务器上下文（线程安全）
 * @return server_context_t* 上下文指针
 */
server_context_t* server_context_get(void);

/**
 * @brief 更新统计信息（线程安全）
 */
void server_stats_inc_request(void);
void server_stats_dec_request(void);
void server_stats_inc_error(void);
void server_stats_inc_ws_connection(void);
void server_stats_inc_ws_disconnect(void);
void server_stats_inc_ws_message_sent(void);
void server_stats_inc_ws_message_failed(void);
void server_stats_inc_rate_limit(void);
void server_stats_inc_validation_failure(void);
void server_stats_inc_timeout(void);
void server_stats_update_heap(void);

/**
 * @brief 获取统计信息副本（线程安全）
 * @param stats 输出统计结构指针
 */
void server_stats_get_copy(server_stats_t *stats);

/**
 * @brief 检查服务器是否正在运行
 */
static inline bool server_is_running(void) {
    server_context_t *ctx = server_context_get();
    return ctx && ctx->state == SERVER_STATE_RUNNING;
}

/**
 * @brief 检查是否正在关闭
 */
static inline bool server_is_shutting_down(void) {
    server_context_t *ctx = server_context_get();
    return ctx && (ctx->state == SERVER_STATE_GRACEFUL_SHUTDOWN || 
                   ctx->graceful_shutdown_pending);
}

#endif // SERVER_CONTEXT_H
