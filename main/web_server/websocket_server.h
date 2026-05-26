/**
 * @file websocket_server.h
 * @brief WebSocket 服务器接口（基于 ESP-IDF httpd_ws API）
 */

#ifndef WEBSOCKET_SERVER_H
#define WEBSOCKET_SERVER_H

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WebSocket 配置
 */
typedef struct {
    void (*on_connect)(int sockfd);       /**< 连接回调 */
    void (*on_disconnect)(int sockfd);    /**< 断开回调 */
    void (*on_message)(int sockfd, const uint8_t *data, size_t len, httpd_ws_type_t type); /**< 消息回调 */
} ws_config_t;

/**
 * @brief 初始化 WebSocket 服务器
 * @param server HTTP 服务器句柄
 * @param config WebSocket 配置
 * @return esp_err_t
 */
esp_err_t websocket_init(httpd_handle_t server, const ws_config_t *config);

/**
 * @brief 发送文本消息
 * @param sockfd 客户端 socket fd
 * @param text 文本
 * @return esp_err_t
 */
esp_err_t websocket_send_text(int sockfd, const char *text);

/**
 * @brief 广播文本消息给所有客户端（异步，线程安全）
 * @param text 文本
 * @return esp_err_t
 */
esp_err_t websocket_broadcast_text(const char *text);

/**
 * @brief 关闭 WebSocket 连接
 * @param sockfd 客户端 socket fd
 * @return esp_err_t
 */
esp_err_t websocket_close(int sockfd);

/**
 * @brief 获取连接客户端数量
 * @return int 客户端数量
 */
int websocket_get_client_count(void);

/**
 * @brief 检查客户端是否连接
 * @param sockfd 客户端 socket fd
 * @return true 已连接
 * @return false 未连接
 */
bool websocket_is_connected(int sockfd);

#ifdef __cplusplus
}
#endif

#endif // WEBSOCKET_SERVER_H
