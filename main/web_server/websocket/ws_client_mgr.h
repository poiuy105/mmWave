/**
 * @file ws_client_mgr.h
 * @brief WebSocket 客户端管理器
 *
 * 负责管理所有 WebSocket 客户端连接，包括：
 * - 客户端状态跟踪
 * - 连接/断开处理
 * - 消息队列管理
 */

#ifndef WS_CLIENT_MGR_H
#define WS_CLIENT_MGR_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/socket.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_http_server.h"

// ==================== 常量定义 ====================

#define WS_CLIENT_IP_LEN 16

// ==================== 客户端状态 ====================

typedef enum {
    WS_CLIENT_STATE_IDLE = 0,
    WS_CLIENT_STATE_CONNECTED,
    WS_CLIENT_STATE_ACTIVE,
    WS_CLIENT_STATE_SENDING,
    WS_CLIENT_STATE_PING_PENDING,
    WS_CLIENT_STATE_DISCONNECTING,
    WS_CLIENT_STATE_DISCONNECTED
} ws_client_state_t;

// ==================== 消息结构 ====================

/**
 * @brief WebSocket 消息结构
 */
typedef struct {
    httpd_ws_type_t type;         // 消息类型
    size_t len;                    // 消息长度
    uint8_t data[];               // 消息数据（柔性数组）
} ws_msg_t;

/**
 * @brief 带数据的消息（用于队列传递）
 */
#define WS_MSG_PAYLOAD_MAX 256  // 缩减以节省 heap（雷达 JSON 通常 <200B）
typedef struct {
    httpd_ws_type_t type;
    size_t len;
    uint8_t payload[WS_MSG_PAYLOAD_MAX];
} ws_msg_queue_item_t;

// ==================== 客户端结构 ====================

/**
 * @brief WebSocket 客户端信息
 */
typedef struct {
    int fd;                         // socket 文件描述符
    volatile bool active;           // 连接激活标志（volatile 保证原子读取）
    ws_client_state_t state;        // 客户端状态
    volatile TickType_t last_activity;  // 最后活动时间
    uint32_t connection_id;         // 连接 ID（用于日志追踪）
    uint32_t msg_count_sent;        // 发送消息计数
    uint32_t msg_count_received;    // 接收消息计数
    uint32_t error_count;           // 错误计数
    char client_ip[WS_CLIENT_IP_LEN];  // 客户端 IP 地址

#define WS_MAX_MSG_QUEUE_SIZE 10  // Max queue depth per client

    // Send queue and task
    QueueHandle_t msg_queue;        // Pending message queue
    TaskHandle_t sender_task;       // Sender task handle
    StaticQueue_t msg_queue_buffer; // Queue static buffer
    uint8_t msg_queue_storage[sizeof(ws_msg_queue_item_t) * WS_MAX_MSG_QUEUE_SIZE];  // Queue storage

    // 统计
    uint32_t bytes_sent;            // 发送字节数
    uint32_t bytes_received;        // 接收字节数
} ws_client_t;

/**
 * @brief 客户端管理器配置
 */
typedef struct {
    uint8_t max_clients;           // 最大客户端数
    uint8_t msg_queue_size;        // 每个客户端的消息队列大小
    uint16_t max_msg_size;         // 最大消息大小
} ws_client_mgr_config_t;

/**
 * @brief 客户端管理器
 */
typedef struct {
    ws_client_t *clients;           // 客户端数组
    uint8_t max_clients;            // 最大客户端数
    SemaphoreHandle_t mutex;        // 互斥量
    StaticSemaphore_t mutex_buffer; // 互斥量静态缓冲区

    // 连接计数
    uint32_t total_connections;     // 历史总连接数
    uint32_t total_disconnections;  // 历史总断开数
    uint32_t total_messages_sent;   // 历史总发送消息数
    uint32_t total_messages_failed; // 历史总发送失败数

    // 配置副本
    ws_client_mgr_config_t config;
} ws_client_mgr_t;

/**
 * @brief 初始化客户端管理器
 * @param mgr 客户端管理器指针
 * @param config 配置
 * @return esp_err_t
 */
esp_err_t ws_client_mgr_init(ws_client_mgr_t *mgr, const ws_client_mgr_config_t *config);

/**
 * @brief 销毁客户端管理器
 * @param mgr 客户端管理器指针
 */
void ws_client_mgr_deinit(ws_client_mgr_t *mgr);

/**
 * @brief 添加客户端连接
 * @param mgr 客户端管理器指针
 * @param fd socket 文件描述符
 * @param client_ip 客户端 IP 地址
 * @return int 客户端索引，失败返回 -1
 */
int ws_client_mgr_add(ws_client_mgr_t *mgr, int fd, const char *client_ip);

/**
 * @brief 移除客户端连接
 * @param mgr 客户端管理器指针
 * @param fd socket 文件描述符
 */
void ws_client_mgr_remove(ws_client_mgr_t *mgr, int fd);

/**
 * @brief 按 fd 查找客户端索引
 * @param mgr 客户端管理器指针
 * @param fd socket 文件描述符
 * @return int 客户端索引，未找到返回 -1
 */
int ws_client_mgr_find_by_fd(ws_client_mgr_t *mgr, int fd);

/**
 * @brief 更新客户端活动时间
 * @param mgr 客户端管理器指针
 * @param fd socket 文件描述符
 */
void ws_client_mgr_update_activity(ws_client_mgr_t *mgr, int fd);

/**
 * @brief 获取活跃客户端数
 * @param mgr 客户端管理器指针
 * @return int 活跃客户端数
 */
int ws_client_mgr_get_active_count(const ws_client_mgr_t *mgr);

/**
 * @brief 检查客户端是否活跃
 * @param mgr 客户端管理器指针
 * @param fd socket 文件描述符
 * @return true 活跃
 * @return false 不活跃或不存在
 */
bool ws_client_mgr_is_active(const ws_client_mgr_t *mgr, int fd);

/**
 * @brief 移除超时客户端
 * @param mgr 客户端管理器指针
 * @param server httpd 服务器句柄
 * @param timeout_ticks 超时时间（ticks）
 * @return int 移除的客户端数
 */
int ws_client_mgr_remove_timeout(ws_client_mgr_t *mgr, httpd_handle_t server, TickType_t timeout_ticks);

/**
 * @brief 广播消息到所有客户端
 * @param mgr 客户端管理器指针
 * @param server httpd 服务器句柄
 * @param data 消息数据
 * @param len 消息长度
 * @param type 消息类型
 * @return int 成功发送的目标数
 */
int ws_client_mgr_broadcast(ws_client_mgr_t *mgr, httpd_handle_t server,
                            const uint8_t *data, size_t len, httpd_ws_type_t type);

/**
 * @brief 发送消息到指定客户端（异步入队）
 * @param mgr 客户端管理器指针
 * @param server httpd 服务器句柄
 * @param fd socket 文件描述符
 * @param data 消息数据
 * @param len 消息长度
 * @param type 消息类型
 * @return esp_err_t
 */
esp_err_t ws_client_mgr_send_async(ws_client_mgr_t *mgr, httpd_handle_t server,
                                   int fd, const uint8_t *data, size_t len, httpd_ws_type_t type);

/**
 * @brief 获取管理器统计信息
 * @param mgr 客户端管理器指针
 * @param total_conn 输出总连接数
 * @param total_disconn 输出总断开数
 * @param total_sent 输出总发送数
 * @param total_failed 输出总失败数
 * @param active 输出活跃客户端数
 */
void ws_client_mgr_get_stats(const ws_client_mgr_t *mgr,
                             uint32_t *total_conn, uint32_t *total_disconn,
                             uint32_t *total_sent, uint32_t *total_failed,
                             int *active);

/**
 * @brief 打印客户端状态（调试用）
 * @param mgr 客户端管理器指针
 */
void ws_client_mgr_dump_status(const ws_client_mgr_t *mgr);

#endif // WS_CLIENT_MGR_H
