/**
 * @file websocket_server.c
 * @brief WebSocket 服务器实现（基于 ESP-IDF httpd_ws API）
 *
 * 使用 ESP-IDF 内置的 WebSocket 支持：
 * - URI 注册时设置 .is_websocket = true，握手由 httpd 自动完成
 * - 使用 httpd_ws_frame_t 和 httpd_ws_send_frame_async 发送帧
 * - 使用 httpd_queue_work 在 httpd 工作队列中异步发送（线程安全）
 * - 使用 httpd_ws_recv_frame 接收帧
 */

#include "websocket_server.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "WS_SERVER";

#define MAX_WS_CLIENTS 4

/* ---------- 客户端连接表 ---------- */

static struct {
    int fd;
    bool active;
} s_clients[MAX_WS_CLIENTS];

static ws_config_t s_config;
static SemaphoreHandle_t s_mutex = NULL;
static httpd_handle_t s_server = NULL;

/* ---------- 客户端管理 ---------- */

static int get_client_index(int fd)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_clients[i].fd == fd && s_clients[i].active) {
            return i;
        }
    }
    return -1;
}

static int add_client(int fd)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (!s_clients[i].active) {
            s_clients[i].fd = fd;
            s_clients[i].active = true;
            return i;
        }
    }
    return -1;
}

static void remove_client(int fd)
{
    int idx = get_client_index(fd);
    if (idx >= 0) {
        s_clients[idx].active = false;
        s_clients[idx].fd = -1;
    }
}

/* ---------- WebSocket handler ---------- */

/**
 * @brief WebSocket URI handler
 *
 * 当 .is_websocket = true 时，ESP-IDF httpd 自动完成 WebSocket 握手。
 * handler 被调用时连接已建立，可以直接收发帧。
 */
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        /* GET 请求 = 新的 WebSocket 连接，httpd 已自动完成握手 */
        int fd = httpd_req_to_sockfd(req);

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        int idx = add_client(fd);
        xSemaphoreGive(s_mutex);

        if (idx < 0) {
            ESP_LOGE(TAG, "Max clients (%d) reached, rejecting fd=%d", MAX_WS_CLIENTS, fd);
            httpd_ws_frame_t close_pkt = {
                .type = HTTPD_WS_TYPE_CLOSE,
                .payload = NULL,
                .len = 0,
            };
            httpd_ws_send_frame(req, &close_pkt);
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "WebSocket client connected: fd=%d (slot %d)", fd, idx);

        if (s_config.on_connect) {
            s_config.on_connect(fd);
        }

        return ESP_OK;
    }

    /* 非 GET 请求，接收 WebSocket 帧 */
    uint8_t buf[1024] = {0};
    httpd_ws_frame_t ws_pkt = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = buf,
        .len = sizeof(buf),
    };

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "httpd_ws_recv_frame failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 如果帧长度为 0，跳过（可能是分片帧的中间分片） */
    if (ws_pkt.len == 0) {
        return ESP_OK;
    }

    ESP_LOGD(TAG, "WS frame: type=%d len=%d from fd=%d", ws_pkt.type, ws_pkt.len,
             httpd_req_to_sockfd(req));

    int fd = httpd_req_to_sockfd(req);

    switch (ws_pkt.type) {
        case HTTPD_WS_TYPE_TEXT:
        case HTTPD_WS_TYPE_BINARY:
            /* 确保以 null 结尾（仅 TEXT） */
            if (ws_pkt.type == HTTPD_WS_TYPE_TEXT && ws_pkt.len < sizeof(buf)) {
                buf[ws_pkt.len] = '\0';
            }
            if (s_config.on_message) {
                s_config.on_message(fd, buf, ws_pkt.len, ws_pkt.type);
            }
            break;

        case HTTPD_WS_TYPE_CLOSE:
            ESP_LOGI(TAG, "WebSocket close frame from fd=%d", fd);
            if (s_config.on_disconnect) {
                s_config.on_disconnect(fd);
            }
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            remove_client(fd);
            xSemaphoreGive(s_mutex);
            /* 回复 close 帧 */
            {
                httpd_ws_frame_t close_pkt = {
                    .type = HTTPD_WS_TYPE_CLOSE,
                    .payload = NULL,
                    .len = 0,
                };
                httpd_ws_send_frame(req, &close_pkt);
            }
            break;

        case HTTPD_WS_TYPE_PING:
            /* 自动回复 pong */
            ESP_LOGD(TAG, "WS ping from fd=%d, sending pong", fd);
            {
                httpd_ws_frame_t pong_pkt = {
                    .type = HTTPD_WS_TYPE_PONG,
                    .payload = ws_pkt.payload,
                    .len = ws_pkt.len,
                };
                httpd_ws_send_frame(req, &pong_pkt);
            }
            break;

        case HTTPD_WS_TYPE_PONG:
            /* 忽略 pong */
            break;

        default:
            ESP_LOGW(TAG, "Unknown WS frame type: %d", ws_pkt.type);
            break;
    }

    return ESP_OK;
}

/* ---------- 公共 API ---------- */

esp_err_t websocket_init(httpd_handle_t server, const ws_config_t *config)
{
    if (server == NULL || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_server = server;
    memcpy(&s_config, config, sizeof(ws_config_t));

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    memset(s_clients, 0, sizeof(s_clients));
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        s_clients[i].fd = -1;
    }

    /* 注册 WebSocket URI，设置 is_websocket = true */
    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = NULL,
        .is_websocket = true,
        .supported_subprotocol = NULL,
    };

    esp_err_t err = httpd_register_uri_handler(server, &ws_uri);
    if (err != ESP_OK) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        s_server = NULL;
        return err;
    }

    ESP_LOGI(TAG, "WebSocket server initialized (ESP-IDF httpd_ws API)");
    return ESP_OK;
}

esp_err_t websocket_send_text(int sockfd, const char *text)
{
    if (s_server == NULL || text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!websocket_is_connected(sockfd)) {
        return ESP_ERR_INVALID_STATE;
    }

    httpd_ws_frame_t ws_pkt = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)text,
        .len = strlen(text),
    };

    esp_err_t ret = httpd_ws_send_frame_async(s_server, sockfd, &ws_pkt);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "send_text to fd=%d failed: %s", sockfd, esp_err_to_name(ret));
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        remove_client(sockfd);
        xSemaphoreGive(s_mutex);
    }

    return ret;
}

esp_err_t websocket_broadcast_text(const char *text)
{
    if (s_server == NULL || s_mutex == NULL || text == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    httpd_ws_frame_t ws_pkt = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)text,
        .len = strlen(text),
    };

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_clients[i].active) {
            int fd = s_clients[i].fd;
            esp_err_t ret = httpd_ws_send_frame_async(s_server, fd, &ws_pkt);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "broadcast to fd=%d failed: %s", fd, esp_err_to_name(ret));
                remove_client(fd);
            }
        }
    }

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t websocket_close(int sockfd)
{
    if (s_server == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!websocket_is_connected(sockfd)) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 直接使用 httpd_ws_send_frame_async 发送 close 帧 */
    httpd_ws_frame_t ws_pkt = {
        .type = HTTPD_WS_TYPE_CLOSE,
        .payload = NULL,
        .len = 0,
    };

    esp_err_t ret = httpd_ws_send_frame_async(s_server, sockfd, &ws_pkt);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "close fd=%d failed: %s", sockfd, esp_err_to_name(ret));
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    remove_client(sockfd);
    xSemaphoreGive(s_mutex);

    return ESP_OK;
}

int websocket_get_client_count(void)
{
    if (s_mutex == NULL) {
        return 0;
    }

    int count = 0;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_clients[i].active) {
            count++;
        }
    }
    xSemaphoreGive(s_mutex);

    return count;
}

bool websocket_is_connected(int sockfd)
{
    if (s_mutex == NULL) {
        return false;
    }

    bool found = false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    found = get_client_index(sockfd) >= 0;
    xSemaphoreGive(s_mutex);

    return found;
}
