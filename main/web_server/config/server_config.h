/**
 * @file server_config.h
 * @brief 服务器配置参数定义（Kconfig 集成）
 *
 * 所有配置参数均可通过 menuconfig 进行设置
 */

#ifndef SERVER_CONFIG_H
#define SERVER_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 服务器配置结构
 *
 * 配置参数说明：
 * - 所有参数都有默认值，可通过 Kconfig 覆盖
 * - 配置在运行时只读，不可修改
 */
typedef struct server_config {
    // ==================== HTTP 服务器配置 ====================

    /** HTTP 服务器启用标志 */
    bool http_enabled;

    /** HTTP 服务器端口 */
    uint16_t http_port;

    /** HTTP 服务器任务栈大小（字节） */
    uint32_t http_stack_size;

    /** 最大 URI 处理器数量 */
    uint8_t http_max_uri_handlers;

    /** 最大同时打开的 socket 数量 */
    uint8_t http_max_open_sockets;

    /** HTTP 接收超时（秒） */
    uint8_t http_recv_timeout;

    /** HTTP 发送超时（秒） */
    uint8_t http_send_timeout;

    /** 最大文件上传大小（字节） */
    uint32_t max_upload_size;

    /** 请求处理超时（毫秒） */
    uint32_t request_timeout_ms;

    // ==================== WebSocket 服务器配置 ====================

    /** WebSocket 服务器启用标志 */
    bool ws_enabled;

    /** 最大 WebSocket 客户端数量 */
    uint8_t ws_max_clients;

    /** 心跳检测间隔（秒） */
    uint8_t ws_heartbeat_interval;

    /** 客户端超时时间（秒） */
    uint16_t ws_client_timeout;

    /** 每个客户端的消息队列大小 */
    uint8_t ws_msg_queue_size;

    /** 最大消息大小（字节） */
    uint16_t ws_max_msg_size;

    /** WebSocket 任务栈大小 */
    uint32_t ws_task_stack_size;

    /** WebSocket 任务优先级 */
    uint8_t ws_task_priority;

    // ==================== 安全配置 ====================

    /** 速率限制启用标志 */
    bool rate_limit_enabled;

    /** 每秒最大请求数 */
    uint16_t rate_limit_max_requests;

    /** 速率限制窗口大小（毫秒） */
    uint16_t rate_limit_window_ms;

    /** 超过限制后的阻塞时间（秒） */
    uint16_t rate_limit_block_duration;

    /** 速率限制表大小（最大 IP 数） */
    uint16_t rate_limit_max_entries;

    /** CORS 启用标志 */
    bool cors_enabled;

    /** CORS 允许的源（域名） */
    char cors_origin[64];

    /** 安全头启用标志 */
    bool security_headers_enabled;

    /** 启用内容类型嗅探保护 */
    bool x_content_type_options;

    /** 启用 X-Frame-Options */
    bool x_frame_options;

    /** 启用内容安全策略 */
    bool content_security_policy;

    // ==================== 广播配置 ====================

    /** 雷达数据广播启用标志 */
    bool broadcast_enabled;

    /** 广播间隔（毫秒） */
    uint16_t broadcast_interval;

    /** 广播任务栈大小 */
    uint32_t broadcast_task_stack;

    /** 广播任务优先级 */
    uint8_t broadcast_task_priority;

    // ==================== 文件系统配置 ====================

    /** FATFS 分区挂载路径 */
    char fatfs_mount_path[32];

    /** 静态文件根目录 */
    char static_file_root[64];

    /** 允许的文件扩展名白名单（JSON 格式） */
    char allowed_extensions[256];

} server_config_t;

/**
 * @brief 加载配置（从 Kconfig 宏）
 * @param config 输出配置结构指针
 * @return esp_err_t
 */
esp_err_t server_config_load(server_config_t *config);

/**
 * @brief 获取默认配置
 * @param config 输出配置结构指针
 */
void server_config_get_defaults(server_config_t *config);

/**
 * @brief 验证配置有效性
 * @param config 配置指针
 * @return true 配置有效
 * @return false 配置无效
 */
bool server_config_validate(const server_config_t *config);

/**
 * @brief 获取配置描述字符串（用于日志）
 * @param config 配置指针
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 */
void server_config_to_string(const server_config_t *config, char *buffer, size_t buffer_size);

#endif // SERVER_CONFIG_H
