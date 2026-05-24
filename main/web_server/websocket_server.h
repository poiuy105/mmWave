/**
 * @file websocket_server.h
 * @brief WebSocket 服务器接口
 */

#ifndef WEBSOCKET_SERVER_H
#define WEBSOCKET_SERVER_H

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WebSocket 帧类型
 */
typedef enum {
    WS_FRAME_TEXT = 0x1,
    WS_FRAME_BINARY = 0x2,
    WS_FRAME_CLOSE = 0x8,
    WS_FRAME_PING = 0x9,
    WS_FRAME_PONG = 0xA
} ws_frame_type_t;

/**
 * @brief WebSocket 消息回调
 */
typedef void (*ws_message_cb_t)(int sockfd, const uint8_t *data, size_t len, ws_frame_type_t type);

/**
 * @brief WebSocket 连接回调
 */
typedef void (*ws_connect_cb_t)(int sockfd);

/**
 * @brief WebSocket 断开回调
 */
typedef void (*ws_disconnect_cb_t)(int sockfd);

/**
 * @brief WebSocket 配置
 */
typedef struct {
    ws_connect_cb_t on_connect;      /**< 连接回调 */
    ws_disconnect_cb_t on_disconnect; /**< 断开回调 */
    ws_message_cb_t on_message;      /**< 消息回调 */
    size_t task_stack_size;          /**< 任务栈大小 */
    int task_priority;               /**< 任务优先级 */
} ws_config_t;

/**
 * @brief 初始化 WebSocket 服务器
 * @param server HTTP 服务器句柄
 * @param config WebSocket 配置
 * @return esp_err_t
 */
esp_err_t websocket_init(httpd_handle_t server, const ws_config_t *config);

/**
 * @brief 发送 WebSocket 消息
 * @param sockfd 客户端 socket
 * @param data 数据
 * @param len 数据长度
 * @param type 帧类型
 * @return esp_err_t
 */
esp_err_t websocket_send(int sockfd, const uint8_t *data, size_t len, ws_frame_type_t type);

/**
 * @brief 发送文本消息
 * @param sockfd 客户端 socket
 * @param text 文本
 * @return esp_err_t
 */
esp_err_t websocket_send_text(int sockfd, const char *text);

/**
 * @brief 广播消息给所有客户端
 * @param data 数据
 * @param len 数据长度
 * @param type 帧类型
 * @return esp_err_t
 */
esp_err_t websocket_broadcast(const uint8_t *data, size_t len, ws_frame_type_t type);

/**
 * @brief 广播文本消息给所有客户端
 * @param text 文本
 * @return esp_err_t
 */
esp_err_t websocket_broadcast_text(const char *text);

/**
 * @brief 关闭 WebSocket 连接
 * @param sockfd 客户端 socket
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
 * @param sockfd 客户端 socket
 * @return true 已连接
 * @return false 未连接
 */
bool websocket_is_connected(int sockfd);

#ifdef __cplusplus
}
#endif

#endif // WEBSOCKET_SERVER_H
