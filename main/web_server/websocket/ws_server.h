/**
 * @file ws_server.h
 * @brief WebSocket 服务器核心接口
 *
 * 工业级重构版 - 基于 ESP-IDF httpd_ws API
 */

#ifndef WS_SERVER_H
#define WS_SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"
#include "ws_client_mgr.h"
#include "ws_heartbeat.h"

// ==================== 回调定义 ====================

/**
 * @brief WebSocket 连接回调
 * @param fd socket 文件描述符
 * @param client_ip 客户端 IP
 */
typedef void (*ws_on_connect_t)(int fd, const char *client_ip);

/**
 * @brief WebSocket 断开回调
 * @param fd socket 文件描述符
 */
typedef void (*ws_on_disconnect_t)(int fd);

/**
 * @brief WebSocket 消息回调
 * @param fd socket 文件描述符
 * @param data 消息数据
 * @param len 消息长度
 * @param type 消息类型
 */
typedef void (*ws_on_message_t)(int fd, const uint8_t *data, size_t len, httpd_ws_type_t type);

/**
 * @brief WebSocket 配置
 */
typedef struct {
    ws_on_connect_t on_connect;     // 连接回调
    ws_on_disconnect_t on_disconnect;  // 断开回调
    ws_on_message_t on_message;     // 消息回调

    // 客户端管理配置
    uint8_t max_clients;           // 最大客户端数
    uint8_t msg_queue_size;        // 消息队列大小
    uint16_t max_msg_size;         // 最大消息大小

    // 心跳配置
    bool heartbeat_enabled;         // 启用心跳
    uint8_t heartbeat_interval;     // 心跳间隔（秒）
    uint16_t heartbeat_timeout;     // 心跳超时（秒）
} ws_server_config_t;

/**
 * @brief WebSocket 服务器句柄
 */
typedef struct ws_server ws_server_t;

/**
 * @brief 创建 WebSocket 服务器
 * @param http_server HTTP 服务器句柄
 * @param config 配置
 * @return ws_server_t* 服务器句柄
 */
ws_server_t* ws_server_create(httpd_handle_t http_server, const ws_server_config_t *config);

/**
 * @brief 销毁 WebSocket 服务器
 * @param server 服务器句柄
 */
void ws_server_destroy(ws_server_t *server);

/**
 * @brief 发送文本消息到客户端
 * @param server 服务器句柄
 * @param fd socket 文件描述符
 * @param text 文本内容
 * @return esp_err_t
 */
esp_err_t ws_server_send_text(ws_server_t *server, int fd, const char *text);

/**
 * @brief 发送二进制消息到客户端
 * @param server 服务器句柄
 * @param fd socket 文件描述符
 * @param data 数据
 * @param len 数据长度
 * @return esp_err_t
 */
esp_err_t ws_server_send_binary(ws_server_t *server, int fd, const uint8_t *data, size_t len);

/**
 * @brief 广播文本消息到所有客户端
 * @param server 服务器句柄
 * @param text 文本内容
 * @return int 成功发送的目标数
 */
int ws_server_broadcast_text(ws_server_t *server, const char *text);

/**
 * @brief 广播二进制消息到所有客户端
 * @param server 服务器句柄
 * @param data 数据
 * @param len 数据长度
 * @return int 成功发送的目标数
 */
int ws_server_broadcast_binary(ws_server_t *server, const uint8_t *data, size_t len);

/**
 * @brief 关闭指定客户端连接
 * @param server 服务器句柄
 * @param fd socket 文件描述符
 * @return esp_err_t
 */
esp_err_t ws_server_close_client(ws_server_t *server, int fd);

/**
 * @brief 获取活跃客户端数量
 * @param server 服务器句柄
 * @return int 客户端数量
 */
int ws_server_get_client_count(ws_server_t *server);

/**
 * @brief 检查客户端是否连接
 * @param server 服务器句柄
 * @param fd socket 文件描述符
 * @return true 已连接
 * @return false 未连接
 */
bool ws_server_is_client_connected(ws_server_t *server, int fd);

/**
 * @brief 获取服务器统计信息
 * @param server 服务器句柄
 * @param total_conn 总连接数
 * @param total_disconn 总断开数
 * @param total_sent 总发送数
 * @param total_failed 总失败数
 * @param active 当前活跃数
 */
void ws_server_get_stats(ws_server_t *server,
                        uint32_t *total_conn, uint32_t *total_disconn,
                        uint32_t *total_sent, uint32_t *total_failed,
                        int *active);

/**
 * @brief 获取客户端 IP 地址
 * @param server 服务器句柄
 * @param fd socket 文件描述符
 * @param ip_buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @return true 成功
 * @return false 失败
 */
bool ws_server_get_client_ip(ws_server_t *server, int fd, char *ip_buffer, size_t buffer_size);

/**
 * @brief 打印服务器状态
 * @param server 服务器句柄
 */
void ws_server_dump_status(ws_server_t *server);

/**
 * @brief 更新客户端活动时间戳（用于心跳超时计算）
 * @param server 服务器句柄
 * @param fd 客户端文件描述符
 */
void ws_server_update_client_activity(ws_server_t *server, int fd);

/**
 * @brief WebSocket URI Handler（供 httpd 注册使用）
 * @param req HTTP 请求
 * @return esp_err_t
 */
esp_err_t ws_uri_handler(httpd_req_t *req);

#endif // WS_SERVER_H
