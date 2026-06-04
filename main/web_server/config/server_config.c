/**
 * @file server_config.c
 * @brief æœåŠ¡å™¨é…ç½®å®žçŽ? */

#include "server_config.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "SERVER_CONFIG";

int server_config_load(server_config_t *config)
{
    if (config == NULL) {
        return -1;
    }

    // èŽ·å–é»˜è®¤å€?    server_config_get_defaults(config);

#ifdef CONFIG_HTTP_SERVER_ENABLED
    config->http_enabled = true;
#endif

#ifdef CONFIG_HTTP_SERVER_PORT
    config->http_port = CONFIG_HTTP_SERVER_PORT;
#endif

#ifdef CONFIG_HTTP_SERVER_STACK_SIZE
    config->http_stack_size = CONFIG_HTTP_SERVER_STACK_SIZE;
#endif

#ifdef CONFIG_HTTP_SERVER_MAX_URI_HANDLERS
    config->http_max_uri_handlers = CONFIG_HTTP_SERVER_MAX_URI_HANDLERS;
#endif

#ifdef CONFIG_HTTP_SERVER_MAX_OPEN_SOCKETS
    config->http_max_open_sockets = CONFIG_HTTP_SERVER_MAX_OPEN_SOCKETS;
#endif

#ifdef CONFIG_HTTP_RECV_TIMEOUT
    config->http_recv_timeout = CONFIG_HTTP_RECV_TIMEOUT;
#endif

#ifdef CONFIG_HTTP_SEND_TIMEOUT
    config->http_send_timeout = CONFIG_HTTP_SEND_TIMEOUT;
#endif

#ifdef CONFIG_MAX_UPLOAD_SIZE
    config->max_upload_size = CONFIG_MAX_UPLOAD_SIZE * 1024;
#endif

    // WebSocket é…ç½®
#ifdef CONFIG_WS_SERVER_ENABLED
    config->ws_enabled = true;
#endif

#ifdef CONFIG_WS_MAX_CLIENTS
    config->ws_max_clients = CONFIG_WS_MAX_CLIENTS;
#endif

#ifdef CONFIG_WS_HEARTBEAT_INTERVAL
    config->ws_heartbeat_interval = CONFIG_WS_HEARTBEAT_INTERVAL;
#endif

#ifdef CONFIG_WS_CLIENT_TIMEOUT
    config->ws_client_timeout = CONFIG_WS_CLIENT_TIMEOUT;
#endif

#ifdef CONFIG_WS_MSG_QUEUE_SIZE
    config->ws_msg_queue_size = CONFIG_WS_MSG_QUEUE_SIZE;
#endif

#ifdef CONFIG_WS_MAX_MSG_SIZE
    config->ws_max_msg_size = CONFIG_WS_MAX_MSG_SIZE;
#endif

#ifdef CONFIG_WS_TASK_STACK_SIZE
    config->ws_task_stack_size = CONFIG_WS_TASK_STACK_SIZE;
#endif

    // å®‰å…¨é…ç½®
#ifdef CONFIG_RATE_LIMIT_ENABLED
    config->rate_limit_enabled = true;
#endif

#ifdef CONFIG_RATE_LIMIT_MAX_REQUESTS
    config->rate_limit_max_requests = CONFIG_RATE_LIMIT_MAX_REQUESTS;
#endif

#ifdef CONFIG_RATE_LIMIT_WINDOW_MS
    config->rate_limit_window_ms = CONFIG_RATE_LIMIT_WINDOW_MS;
#endif

#ifdef CONFIG_RATE_LIMIT_BLOCK_DURATION
    config->rate_limit_block_duration = CONFIG_RATE_LIMIT_BLOCK_DURATION;
#endif

#ifdef CONFIG_RATE_LIMIT_MAX_ENTRIES
    config->rate_limit_max_entries = CONFIG_RATE_LIMIT_MAX_ENTRIES;
#endif

#ifdef CONFIG_CORS_ENABLED
    config->cors_enabled = true;
#endif

#ifdef CONFIG_CORS_ORIGIN
    strncpy(config->cors_origin, CONFIG_CORS_ORIGIN, sizeof(config->cors_origin) - 1);
#endif

#ifdef CONFIG_SECURITY_HEADERS_ENABLED
    config->security_headers_enabled = true;
#endif

#ifdef CONFIG_X_CONTENT_TYPE_OPTIONS
    config->x_content_type_options = true;
#endif

#ifdef CONFIG_X_FRAME_OPTIONS
    config->x_frame_options = true;
#endif

#ifdef CONFIG_CONTENT_SECURITY_POLICY
    config->content_security_policy = true;
#endif

    // å¹¿æ’­é…ç½®
#ifdef CONFIG_RADAR_BROADCAST_ENABLED
    config->broadcast_enabled = true;
#endif

#ifdef CONFIG_RADAR_BROADCAST_INTERVAL
    config->broadcast_interval = CONFIG_RADAR_BROADCAST_INTERVAL;
#endif

#ifdef CONFIG_RADAR_BROADCAST_TASK_STACK
    config->broadcast_task_stack = CONFIG_RADAR_BROADCAST_TASK_STACK;
#endif

    ESP_LOGI(TAG, "Configuration loaded successfully");

    return 0;
}

void server_config_get_defaults(server_config_t *config)
{
    memset(config, 0, sizeof(server_config_t));

    // HTTP é»˜è®¤å€?    config->http_enabled = true;
    config->http_port = 80;
    config->http_stack_size = 8192;   // HTTP server 任务栈大小（无 TLS 场景足够）
    config->http_max_uri_handlers = 64;
    config->http_max_open_sockets = 12;  // 增加以支持更多并发连接
    config->http_recv_timeout = 10;      // 10s，加快 socket 回收
    config->http_send_timeout = 10;      // 10s，加快 socket 回收
    config->max_upload_size = 100 * 1024;
    config->request_timeout_ms = 5000;

    // WebSocket é»˜è®¤å€?    config->ws_enabled = true;
    config->ws_max_clients = 4;
    config->ws_heartbeat_interval = 30;
    config->ws_client_timeout = 60;
    config->ws_msg_queue_size = 10;
    config->ws_max_msg_size = 2048;
    config->ws_task_stack_size = 2048;  // 缩减以节省 heap（心跳任务工作量小）
    config->ws_task_priority = 5;

    // å®‰å…¨é»˜è®¤å€?    config->rate_limit_enabled = true;
    config->rate_limit_max_requests = 20;
    config->rate_limit_window_ms = 1000;
    config->rate_limit_block_duration = 5;
    config->rate_limit_max_entries = 32;
    config->cors_enabled = false;
    strcpy(config->cors_origin, "*");
    config->security_headers_enabled = true;
    config->x_content_type_options = true;
    config->x_frame_options = true;
    config->content_security_policy = false;

    // å¹¿æ’­é»˜è®¤å€?    config->broadcast_enabled = true;
    config->broadcast_interval = 100;
    config->broadcast_task_stack = 4096;
    config->broadcast_task_priority = 5;

    // æ–‡ä»¶ç³»ç»Ÿ
    strcpy(config->fatfs_mount_path, "/storage");
    strcpy(config->static_file_root, "/www");
    strcpy(config->allowed_extensions, ".html,.htm,.css,.js,.json,.png,.jpg,.jpeg,.gif,.svg,.ico,.woff,.woff2,.ttf");
}

bool server_config_validate(server_config_t *config)
{
    if (config->http_port == 0 || config->http_port > 65535) {
        ESP_LOGE(TAG, "Invalid HTTP port: %d", config->http_port);
        return false;
    }

    if (config->http_stack_size < 4096) {
        ESP_LOGE(TAG, "HTTP stack size too small: %lu", config->http_stack_size);
        return false;
    }

    if (config->ws_max_clients == 0 || config->ws_max_clients > 16) {
        ESP_LOGE(TAG, "Invalid WS max clients: %d", config->ws_max_clients);
        return false;
    }

    if (config->ws_client_timeout < 10) {
        ESP_LOGE(TAG, "WS client timeout too small: %d", config->ws_client_timeout);
        return false;
    }

    if (config->ws_max_msg_size < 256 || config->ws_max_msg_size > 65536) {
        ESP_LOGE(TAG, "Invalid WS max msg size: %d", config->ws_max_msg_size);
        return false;
    }

    // 广播配置验证（防止未初始化导致 FreeRTOS 断言失败）
    if (config->broadcast_task_priority < 1 || config->broadcast_task_priority > 24) {
        ESP_LOGW(TAG, "Invalid broadcast_task_priority: %d, resetting to 5", config->broadcast_task_priority);
        config->broadcast_task_priority = 5;
    }

    if (config->broadcast_interval < 50 || config->broadcast_interval > 5000) {
        ESP_LOGW(TAG, "Invalid broadcast_interval: %d, resetting to 100", config->broadcast_interval);
        config->broadcast_interval = 100;
    }

    if (config->broadcast_task_stack < 2048 || config->broadcast_task_stack > 32768) {
        ESP_LOGW(TAG, "Invalid broadcast_task_stack: %lu, resetting to 4096", (unsigned long)config->broadcast_task_stack);
        config->broadcast_task_stack = 4096;
    }

    return true;
}

void server_config_to_string(const server_config_t *config, char *buffer, size_t buffer_size)
{
    snprintf(buffer, buffer_size,
        "HTTP: enabled=%d, port=%d, stack=%lu, sockets=%d, recv=%d, send=%d\n"
        "WS: enabled=%d, clients=%d, hb=%d, timeout=%d, queue=%d, maxmsg=%d\n"
        "Security: rl=%d, maxreq=%d, cors=%d, hdrs=%d\n"
        "Broadcast: en=%d, interval=%d",
        config->http_enabled, config->http_port, (unsigned long)config->http_stack_size,
        config->http_max_open_sockets, config->http_recv_timeout, config->http_send_timeout,
        config->ws_enabled, config->ws_max_clients, config->ws_heartbeat_interval,
        config->ws_client_timeout, config->ws_msg_queue_size, config->ws_max_msg_size,
        config->rate_limit_enabled, config->rate_limit_max_requests,
        config->cors_enabled, config->security_headers_enabled,
        config->broadcast_enabled, config->broadcast_interval
    );
}
