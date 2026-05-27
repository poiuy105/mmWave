/**
 * @file http_server_core.h
 * @brief HTTP 服务器核心接口
 *
 * 负责：
 * - HTTP 服务器的创建和销毁
 * - URI handlers 的注册
 * - 优雅关闭支持
 */

#ifndef HTTP_SERVER_CORE_H
#define HTTP_SERVER_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"
#include "server_context.h"
#include "server_config.h"

// ==================== 前向声明 ====================

typedef struct http_server http_server_t;

// ==================== HTTP 服务器 API ====================

/**
 * @brief 创建 HTTP 服务器
 * @param config 服务器配置
 * @return http_server_t* 服务器句柄
 */
http_server_t* http_server_create(const server_config_t *config);

/**
 * @brief 启动 HTTP 服务器
 * @param server 服务器句柄
 * @return esp_err_t
 */
esp_err_t http_server_start(http_server_t *server);

/**
 * @brief 停止 HTTP 服务器
 * @param server 服务器句柄
 * @return esp_err_t
 */
esp_err_t http_server_stop(http_server_t *server);

/**
 * @brief 优雅停止（等待现有请求完成）
 * @param server 服务器句柄
 * @param timeout_ms 超时时间（毫秒）
 * @return esp_err_t
 */
esp_err_t http_server_graceful_stop(http_server_t *server, uint32_t timeout_ms);

/**
 * @brief 销毁 HTTP 服务器
 * @param server 服务器句柄
 */
void http_server_destroy(http_server_t *server);

/**
 * @brief 获取 HTTP 服务器句柄
 * @param server 服务器句柄
 * @return httpd_handle_t
 */
httpd_handle_t http_server_get_handle(http_server_t *server);

/**
 * @brief 检查服务器是否运行
 * @param server 服务器句柄
 * @return true 运行中
 * @return false 未运行
 */
bool http_server_is_running(http_server_t *server);

/**
 * @brief 获取服务器统计
 * @param server 服务器句柄
 * @param total_requests 总请求数
 * @param active_requests 活跃请求数
 * @param error_requests 错误请求数
 */
void http_server_get_stats(http_server_t *server,
                          uint32_t *total_requests,
                          uint32_t *active_requests,
                          uint32_t *error_requests);

#endif // HTTP_SERVER_CORE_H
