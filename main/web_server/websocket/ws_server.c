/**
 * @file ws_server.c
 * @brief WebSocket server core implementation
 */

#include "ws_server.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "WS_SERVER";

struct ws_server {
    httpd_handle_t http_server;
    ws_client_mgr_t client_mgr;

    ws_on_connect_t on_connect;
    ws_on_disconnect_t on_disconnect;
    ws_on_message_t on_message;

    ws_heartbeat_ctx_t heartbeat_ctx;
    bool heartbeat_enabled;

    uint32_t stats_messages_sent;
    uint32_t stats_messages_failed;
};

static ws_server_t *s_global_server = NULL;

esp_err_t ws_uri_handler(httpd_req_t *req)
{
    ws_server_t *server = (ws_server_t *)req->user_ctx;
    if (server == NULL) {
        server = s_global_server;
    }

    if (req->method == HTTP_GET) {
        int fd = httpd_req_to_sockfd(req);

        int idx = ws_client_mgr_add(&server->client_mgr, fd, "unknown");
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

        ESP_LOGI(TAG, "WebSocket connected: fd=%d, slot=%d", fd, idx);

        if (server->on_connect) {
            server->on_connect(fd, "unknown");
        }

        return ESP_OK;
    }

    int fd = httpd_req_to_sockfd(req);

    if (!ws_client_mgr_is_active(&server->client_mgr, fd)) {
        ESP_LOGW(TAG, "Message from unknown fd=%d", fd);
        return ESP_ERR_NOT_FOUND;
    }

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

    if (ws_pkt.len == 0) {
        return ESP_OK;
    }

    ws_client_mgr_update_activity(&server->client_mgr, fd);

    switch (ws_pkt.type) {
        case HTTPD_WS_TYPE_TEXT:
        case HTTPD_WS_TYPE_BINARY: {
            if (ws_pkt.type == HTTPD_WS_TYPE_TEXT && ws_pkt.len < sizeof(buf)) {
                buf[ws_pkt.len] = '\0';
            }
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
            httpd_ws_frame_t close_pkt = {
                .type = HTTPD_WS_TYPE_CLOSE,
                .payload = NULL,
                .len = 0,
            };
            httpd_ws_send_frame(req, &close_pkt);
            break;
        }

        case HTTPD_WS_TYPE_PING: {
            httpd_ws_frame_t pong_pkt = {
                .type = HTTPD_WS_TYPE_PONG,
                .payload = ws_pkt.payload,
                .len = ws_pkt.len,
            };
            httpd_ws_send_frame(req, &pong_pkt);
            break;
        }

        case HTTPD_WS_TYPE_PONG: {
            ws_client_mgr_update_activity(&server->client_mgr, fd);
            break;
        }

        default:
            break;
    }

    return ESP_OK;
}

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
    server->on_connect = config->on_connect;
    server->on_disconnect = config->on_disconnect;
    server->on_message = config->on_message;

    ws_client_mgr_config_t mgr_config = {
        .max_clients = config->max_clients,
        .msg_queue_size = config->msg_queue_size,
        .max_msg_size = config->max_msg_size,
    };

    if (ws_client_mgr_init(&server->client_mgr, &mgr_config) != ESP_OK) {
        free(server);
        return NULL;
    }

    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_uri_handler,
        .user_ctx = server,
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

    s_global_server = server;

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
            ESP_LOGI(TAG, "Heartbeat enabled: interval=%u us, timeout=%u us",
                     (unsigned int)config->heartbeat_interval,
                     (unsigned int)config->heartbeat_timeout);
        }
    }

    ESP_LOGI(TAG, "WebSocket server created: max_clients=%d, msg_queue=%d, max_msg=%d",
             config->max_clients,
             config->msg_queue_size,
             config->max_msg_size);

    return server;
}

void ws_server_destroy(ws_server_t *server)
{
    if (server == NULL) return;

    if (server->heartbeat_enabled) {
        ws_heartbeat_stop(&server->heartbeat_ctx);
    }

    ws_client_mgr_deinit(&server->client_mgr);

    if (s_global_server == server) {
        s_global_server = NULL;
    }

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
        ESP_LOGI(TAG, "Heartbeat: pings=%lu, timeouts=%lu, pongs=%lu",
                 (unsigned long)pings, (unsigned long)timeouts, (unsigned long)pongs);
    }

    ESP_LOGI(TAG, "Server stats: msg_sent=%lu, msg_failed=%lu",
             (unsigned long)server->stats_messages_sent,
             (unsigned long)server->stats_messages_failed);
}
