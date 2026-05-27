/**
 * @file ws_server.c
 * @brief WebSocket 服务器核心实现
 */

#include "ws_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static const char *TAG = "WS_SERVER";

/**
 * @brief WebSocket 服务器结构
 */
struct ws_server {
    httpd_handle_t http_server;     // HTTP 服务器句柄
    ws_client_mgr_t client_mgr;     // 客户端管理器

    // 回调
    ws_on_connect_t on_connect;     // 连接回调
    ws_on_disconnect_t on_disconnect;  // 断开回调
    ws_on_message_t on_message;     // 消息回调

    // 心跳
    ws_heartbeat_ctx_t heartbeat_ctx;
    bool heartbeat_enabled;

    // 统计
    uint32_t stats_messages_sent;
    uint32_t stats_messages_failed;
};

// ==================== URI Handler ====================

esp_err_t ws_uri_handler(httpd_req_t *req)
{
    ws_server_t *server = (ws_server_t *)httpd_get_global_user_ctx(req->handle());

    if (req->method == HTTP_GET) {
        // 新的 WebSocket 连接
        int fd = httpd_req_to_sockfd(req);

        // 获取客户端 IP
        char client_ip[WS_CLIENT_IP_LEN] = {0};
        struct sockaddr_in6 addr;
        socklen_t addr_len = sizeof(addr);
        if (getpeername(fd, (struct sockaddr *)&addr, &addr_len) == 0) {
            if (addr.sin6_family == AF_INET) {
                struct sockaddr_in *s = (struct sockaddr_in *)&addr;
                inet_ntoa_r(s->sin_addr, client_ip, sizeof(client_ip));
            } else {
                snprintf(client_ip, sizeof(client_ip), "IPv6");
            }
        }

        // 添加客户端
        int idx = ws_client_mgr_add(&server->client_mgr, fd, client_ip);
        if (idx < 0) {
            ESP_LOGW(TAG, "Max clients reached, rejecting fd=%d", fd);
            httpd_ws_frame_t close_pkt = {
                .type = HTTPD_WS_TYPE_CLOSE,
                .payload = NULL,
                .len = 0,
            };
            httpd_ws_send_frame(req, &close_pkt);
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "WebSocket connected: fd=%d, slot=%d, ip=%s", fd, idx, client_ip);

        // 调用连接回调
        if (server->on_connect) {
            server->on_connect(fd, client_ip);
        }

        return ESP_OK;
    }

    // 非 GET 请求 = 接收 WebSocket 帧
    int fd = httpd_req_to_sockfd(req);

    // 检查客户端是否存在
    if (!ws_client_mgr_is_active(&server->client_mgr, fd)) {
        ESP_LOGW(TAG, "Message from unknown fd=%d", fd);
        return ESP_ERR_NOT_FOUND;
    }

    // 接收帧
    uint8_t buf[2048] = {0};
    httpd_ws_frame_t ws_pkt = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = buf,
        .len = sizeof(buf),
    };

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to receive frame from fd=%d: %s", fd, esp_err_to_name(ret));
        return ret;
    }

    // 空帧（分片中间帧）
    if (ws_pkt.len == 0) {
        return ESP_OK;
    }

    // 更新活动时间
    ws_client_mgr_update_activity(&server->client_mgr, fd);

    // 处理不同类型的帧
    switch (ws_pkt.type) {
        case HTTPD_WS_TYPE_TEXT:
        case HTTPD_WS_TYPE_BINARY: {
            // 确保文本消息 null 结尾
            if (ws_pkt.type == HTTPD_WS_TYPE_TEXT && ws_pkt.len < sizeof(buf)) {
                buf[ws_pkt.len] = '\0';
            }

            // 调用消息回调
            if (server->on_message) {
                server->on_message(fd, buf, ws_pkt.len, ws_pkt.type);
            }
            break;
        }

        case HTTPD_WS_TYPE_CLOSE: {
            ESP_LOGI(TAG, "Close frame from fd=%d", fd);
            ws_client_mgr_remove(&server->client_mgr, fd);

            if (server->on_disconnect) {
                server->on_disconnect(fd);
            }

            // 回复 close 帧
            httpd_ws_frame_t close_pkt = {
                .type = HTTPD_WS_TYPE_CLOSE,
                .payload = NULL,
                .len = 0,
            };
            httpd_ws_send_frame(req, &close_pkt);
            break;
        }

        case HTTPD_WS_TYPE_PING: {
            ESP_LOGD(TAG, "Ping from fd=%d, sending pong", fd);

            // 自动回复 pong
            httpd_ws_frame_t pong_pkt = {
                .type = HTTPD_WS_TYPE_PONG,
                .payload = ws_pkt.payload,
                .len = ws_pkt.len,
            };
            httpd_ws_send_frame(req, &pong_pkt);
            break;
        }

        case HTTPD_WS_TYPE_PONG: {
            // Pong 响应，更新活动时间
            ws_client_mgr_update_activity(&server->client_mgr, fd);
            ESP_LOGD(TAG, "Pong from fd=%d", fd);
            break;
        }

        case HTTPD_WS_TYPE_CONTINUATION: {
            ESP_LOGW(TAG, "Continuation frame not supported, ignoring");
            break;
        }

        default:
            ESP_LOGW(TAG, "Unknown frame type: %d from fd=%d", ws_pkt.type, fd);
            break;
    }

    return ESP_OK;
}

// ==================== Public API ====================

ws_server_t* ws_server_create(httpd_handle_t http_server, const ws_server_config_t *config)
{
    if (http_server == NULL || config == NULL) {
        ESP_LOGE(TAG, "Invalid arguments");
        return NULL;
    }

    ws_server_t *server = calloc(1, sizeof(ws_server_t));
    if (server == NULL) {
        ESP_LOGE(TAG, "Failed to allocate server");
        return NULL;
    }

    server->http_server = http_server;

    // 复制回调
    server->on_connect = config->on_connect;
    server->on_disconnect = config->on_disconnect;
    server->on_message = config->on_message;

    // 初始化客户端管理器
    ws_client_mgr_config_t mgr_config = {
        .max_clients = config->max_clients,
        .msg_queue_size = config->msg_queue_size,
        .max_msg_size = config->max_msg_size,
    };

    if (ws_client_mgr_init(&server->client_mgr, &mgr_config) != ESP_OK) {
        free(server);
        return NULL;
    }

    // 注册 WebSocket URI
    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_uri_handler,
        .user_ctx = server,  // 传递服务器句柄
        .is_websocket = true,
        .supported_subprotocol = NULL,
    };

    esp_err_t err = httpd_register_uri_handler(http_server, &ws_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WS URI: %s", esp_err_to_name(err));
        ws_client_mgr_deinit(&server->client_mgr);
        free(server);
        return NULL;
    }

    // 设置全局用户上下文，供 handler 访问
    httpd_set_user_ctx(http_server, server);

    // 初始化并启动心跳
    server->heartbeat_enabled = config->heartbeat_enabled;
    if (config->heartbeat_enabled) {
        ws_heartbeat_config_t hb_config = {
            .check_interval_sec = config->heartbeat_interval,
            .client_timeout_sec = config->heartbeat_timeout,
            .auto_ping = true,
        };

        if (ws_heartbeat_init(&server->heartbeat_ctx, &server->client_mgr,
                              http_server, &hb_config) == ESP_OK) {
            ws_heartbeat_start(&server->heartbeat_ctx);
            ESP_LOGI(TAG, "Heartbeat enabled: interval=%us, timeout=%us",
                     config->heartbeat_interval, config->heartbeat_timeout);
        }
    }

    ESP_LOGI(TAG, "WebSocket server created: max_clients=%d, msg_queue=%d, max_msg=%d",
             config->max_clients, config->msg_queue_size, config->max_msg_size);

    return server;
}

void ws_server_destroy(ws_server_t *server)
{
    if (server == NULL) return;

    // 停止心跳
    if (server->heartbeat_enabled) {
        ws_heartbeat_stop(&server->heartbeat_ctx);
    }

    // 销毁客户端管理器
    ws_client_mgr_deinit(&server->client_mgr);

    free(server);
    ESP_LOGI(TAG, "WebSocket server destroyed");
}

esp_err_t ws_server_send_text(ws_server_t *server, int fd, const char *text)
{
    if (server == NULL || fd < 0 || text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!ws_client_mgr_is_active(&server->client_mgr, fd)) {
        return ESP_ERR_INVALID_STATE;
    }

    httpd_ws_frame_t ws_pkt = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)text,
        .len = strlen(text),
    };

    esp_err_t ret = httpd_ws_send_frame_async(server->http_server, fd, &ws_pkt);

    if (ret == ESP_OK) {
        server->stats_messages_sent++;
    } else {
        server->stats_messages_failed++;
        ESP_LOGW(TAG, "Send text failed: fd=%d, ret=%s", fd, esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t ws_server_send_binary(ws_server_t *server, int fd, const uint8_t *data, size_t len)
{
    if (server == NULL || fd < 0 || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!ws_client_mgr_is_active(&server->client_mgr, fd)) {
        return ESP_ERR_INVALID_STATE;
    }

    httpd_ws_frame_t ws_pkt = {
        .type = HTTPD_WS_TYPE_BINARY,
        .payload = (uint8_t *)data,
        .len = len,
    };

    esp_err_t ret = httpd_ws_send_frame_async(server->http_server, fd, &ws_pkt);

    if (ret == ESP_OK) {
        server->stats_messages_sent++;
    } else {
        server->stats_messages_failed++;
    }

    return ret;
}

int ws_server_broadcast_text(ws_server_t *server, const char *text)
{
    if (server == NULL || text == NULL) {
        return 0;
    }

    int count = ws_client_mgr_broadcast(&server->client_mgr, server->http_server,
                                        (const uint8_t *)text, strlen(text),
                                        HTTPD_WS_TYPE_TEXT);
    server->stats_messages_sent += count;
    server->stats_messages_failed += (ws_client_mgr_get_active_count(&server->client_mgr) - count);

    return count;
}

int ws_server_broadcast_binary(ws_server_t *server, const uint8_t *data, size_t len)
{
    if (server == NULL || data == NULL || len == 0) {
        return 0;
    }

    int count = ws_client_mgr_broadcast(&server->client_mgr, server->http_server,
                                        data, len, HTTPD_WS_TYPE_BINARY);
    server->stats_messages_sent += count;
    server->stats_messages_failed += (ws_client_mgr_get_active_count(&server->client_mgr) - count);

    return count;
}

esp_err_t ws_server_close_client(ws_server_t *server, int fd)
{
    if (server == NULL || fd < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!ws_client_mgr_is_active(&server->client_mgr, fd)) {
        return ESP_ERR_INVALID_STATE;
    }

    httpd_ws_frame_t close_pkt = {
        .type = HTTPD_WS_TYPE_CLOSE,
        .payload = NULL,
        .len = 0,
    };

    esp_err_t ret = httpd_ws_send_frame_async(server->http_server, fd, &close_pkt);

    if (ret == ESP_OK) {
        ws_client_mgr_remove(&server->client_mgr, fd);
    }

    return ret;
}

int ws_server_get_client_count(ws_server_t *server)
{
    if (server == NULL) {
        return 0;
    }
    return ws_client_mgr_get_active_count(&server->client_mgr);
}

bool ws_server_is_client_connected(ws_server_t *server, int fd)
{
    if (server == NULL || fd < 0) {
        return false;
    }
    return ws_client_mgr_is_active(&server->client_mgr, fd);
}

void ws_server_get_stats(ws_server_t *server,
                        uint32_t *total_conn, uint32_t *total_disconn,
                        uint32_t *total_sent, uint32_t *total_failed,
                        int *active)
{
    if (server == NULL) return;

    ws_client_mgr_get_stats(&server->client_mgr, total_conn, total_disconn,
                           total_sent, total_failed, active);

    if (total_sent) *total_sent += server->stats_messages_sent;
    if (total_failed) *total_failed += server->stats_messages_failed;
}

bool ws_server_get_client_ip(ws_server_t *server, int fd, char *ip_buffer, size_t buffer_size)
{
    if (server == NULL || fd < 0 || ip_buffer == NULL || buffer_size == 0) {
        return false;
    }

    xSemaphoreTake(server->client_mgr.mutex, portMAX_DELAY);

    int idx = ws_client_mgr_find_by_fd(&server->client_mgr, fd);
    if (idx >= 0) {
        strncpy(ip_buffer, server->client_mgr.clients[idx].client_ip, buffer_size - 1);
        ip_buffer[buffer_size - 1] = '\0';
    }

    xSemaphoreGive(server->client_mgr.mutex);

    return idx >= 0;
}

void ws_server_dump_status(ws_server_t *server)
{
    if (server == NULL) return;

    ESP_LOGI(TAG, "=== WebSocket Server Status ===");
    ws_client_mgr_dump_status(&server->client_mgr);

    if (server->heartbeat_enabled) {
        uint32_t pings, timeouts, pongs;
        ws_heartbeat_get_stats(&server->heartbeat_ctx, &pings, &timeouts, &pongs);
        ESP_LOGI(TAG, "Heartbeat stats: pings_sent=%lu, timeouts=%lu, pongs_received=%lu",
                 pings, timeouts, pongs);
    }

    ESP_LOGI(TAG, "Server stats: msg_sent=%lu, msg_failed=%lu",
             server->stats_messages_sent, server->stats_messages_failed);
}
