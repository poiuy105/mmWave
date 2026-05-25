/**
 * @file websocket_server.c
 * @brief WebSocket 服务器实现
 */

#include "websocket_server.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include "mbedtls/sha1.h"
#include "mbedtls/base64.h"

static const char *TAG = "WS_SERVER";

// WebSocket magic string (RFC 6455)
static const char WS_MAGIC[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

/**
 * @brief 计算 Sec-WebSocket-Accept
 * @param client_key 客户端发送的 Sec-WebSocket-Key
 * @param accept_key 输出缓冲区
 * @param accept_key_len 缓冲区大小
 */
static void compute_accept_key(const char *client_key, char *accept_key, size_t accept_key_len)
{
    char concat[256];
    uint8_t sha1_hash[20];
    size_t olen;
    
    // client_key + magic
    int len = snprintf(concat, sizeof(concat), "%s%s", client_key, WS_MAGIC);
    
    // SHA1
    mbedtls_sha1_context ctx;
    mbedtls_sha1_init(&ctx);
    mbedtls_sha1_starts(&ctx);
    mbedtls_sha1_update(&ctx, (const uint8_t *)concat, len);
    mbedtls_sha1_finish(&ctx, sha1_hash);
    mbedtls_sha1_free(&ctx);
    
    // Base64 encode
    mbedtls_base64_encode((uint8_t *)accept_key, accept_key_len, &olen, sha1_hash, 20);
}

#define MAX_WS_CLIENTS 4
#define WS_BUF_SIZE 1024

// WebSocket 操作码
#define WS_OP_CONT  0x0
#define WS_OP_TEXT  0x1
#define WS_OP_BIN   0x2
#define WS_OP_CLOSE 0x8
#define WS_OP_PING  0x9
#define WS_OP_PONG  0xA

// 客户端连接表
static struct {
    int sockfd;
    bool active;
    uint8_t mask_key[4];
} s_clients[MAX_WS_CLIENTS];

static ws_config_t s_config;
static SemaphoreHandle_t s_mutex = NULL;

/**
 * @brief 获取客户端索引
 */
static int get_client_index(int sockfd)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_clients[i].sockfd == sockfd && s_clients[i].active) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief 添加客户端
 */
static int add_client(int sockfd)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (!s_clients[i].active) {
            s_clients[i].sockfd = sockfd;
            s_clients[i].active = true;
            xSemaphoreGive(s_mutex);
            return i;
        }
    }
    
    xSemaphoreGive(s_mutex);
    return -1;
}

/**
 * @brief 移除客户端
 */
static void remove_client(int sockfd)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    
    int idx = get_client_index(sockfd);
    if (idx >= 0) {
        s_clients[idx].active = false;
        s_clients[idx].sockfd = -1;
    }
    
    xSemaphoreGive(s_mutex);
}

/**
 * @brief 编码 WebSocket 帧
 */
static size_t encode_ws_frame(uint8_t *out, const uint8_t *data, size_t len, uint8_t opcode)
{
    size_t pos = 0;
    
    // FIN + Opcode
    out[pos++] = 0x80 | opcode;
    
    // Mask + Payload length
    if (len < 126) {
        out[pos++] = len;
    } else if (len < 65536) {
        out[pos++] = 126;
        out[pos++] = (len >> 8) & 0xFF;
        out[pos++] = len & 0xFF;
    } else {
        out[pos++] = 127;
        for (int i = 7; i >= 0; i--) {
            out[pos++] = (len >> (i * 8)) & 0xFF;
        }
    }
    
    // Payload
    memcpy(out + pos, data, len);
    pos += len;
    
    return pos;
}

/**
 * @brief 解码 WebSocket 帧
 */
static int decode_ws_frame(const uint8_t *in, size_t in_len, uint8_t *out, size_t *out_len, uint8_t *opcode)
{
    if (in_len < 2) return 0; // 需要更多数据
    
    size_t pos = 0;
    
    // FIN + RSV + Opcode
    *opcode = in[pos] & 0x0F;
    bool masked = (in[pos + 1] & 0x80) != 0;
    uint64_t payload_len = in[pos + 1] & 0x7F;
    pos += 2;
    
    // Extended payload length
    if (payload_len == 126) {
        if (in_len < 4) return 0;
        payload_len = ((uint64_t)in[pos] << 8) | in[pos + 1];
        pos += 2;
    } else if (payload_len == 127) {
        if (in_len < 10) return 0;
        payload_len = 0;
        for (int i = 0; i < 8; i++) {
            payload_len = (payload_len << 8) | in[pos + i];
        }
        pos += 8;
    }
    
    // Mask key
    uint8_t mask_key[4] = {0};
    if (masked) {
        if (in_len < pos + 4) return 0;
        memcpy(mask_key, in + pos, 4);
        pos += 4;
    }
    
    // Check if we have all data
    if (in_len < pos + payload_len) return 0;
    
    // Unmask payload
    if (masked) {
        for (uint64_t i = 0; i < payload_len; i++) {
            out[i] = in[pos + i] ^ mask_key[i % 4];
        }
    } else {
        memcpy(out, in + pos, payload_len);
    }
    
    *out_len = payload_len;
    return pos + payload_len;
}

/**
 * @brief WebSocket 处理任务
 */
static void ws_handler_task(void *pvParameters)
{
    int sockfd = (int)pvParameters;
    uint8_t rx_buf[WS_BUF_SIZE];
    uint8_t payload_buf[WS_BUF_SIZE];
    size_t rx_pos = 0;
    
    ESP_LOGI(TAG, "WS handler started for fd=%d", sockfd);
    
    // 通知连接
    if (s_config.on_connect) {
        s_config.on_connect(sockfd);
    }
    
    while (1) {
        // 接收数据
        int received = recv(sockfd, rx_buf + rx_pos, WS_BUF_SIZE - rx_pos - 1, 0);
        
        if (received <= 0) {
            ESP_LOGI(TAG, "Client disconnected: fd=%d", sockfd);
            break;
        }
        
        rx_pos += received;
        
        // 处理帧
        size_t processed = 0;
        while (processed < rx_pos) {
            uint8_t opcode;
            size_t payload_len;
            
            int frame_len = decode_ws_frame(rx_buf + processed, rx_pos - processed,
                                           payload_buf, &payload_len, &opcode);
            
            if (frame_len == 0) break; // 需要更多数据
            
            processed += frame_len;
            
            // 处理 opcode
            switch (opcode) {
                case WS_OP_TEXT:
                case WS_OP_BIN:
                    payload_buf[payload_len] = '\0';
                    if (s_config.on_message) {
                        s_config.on_message(sockfd, payload_buf, payload_len, 
                                          opcode == WS_OP_TEXT ? WS_FRAME_TEXT : WS_FRAME_BINARY);
                    }
                    break;
                    
                case WS_OP_CLOSE:
                    ESP_LOGI(TAG, "Close frame received: fd=%d", sockfd);
                    goto cleanup;
                    
                case WS_OP_PING:
                    // 发送 Pong
                    {
                        uint8_t pong_frame[2] = {0x8A, 0x00};
                        send(sockfd, pong_frame, 2, 0);
                    }
                    break;
                    
                case WS_OP_PONG:
                    // 忽略 Pong
                    break;
                    
                default:
                    ESP_LOGW(TAG, "Unknown opcode: %d", opcode);
                    break;
            }
        }
        
        // 移动剩余数据
        if (processed > 0 && processed < rx_pos) {
            memmove(rx_buf, rx_buf + processed, rx_pos - processed);
        }
        rx_pos -= processed;
    }
    
cleanup:
    // 通知断开
    if (s_config.on_disconnect) {
        s_config.on_disconnect(sockfd);
    }
    
    remove_client(sockfd);
    close(sockfd);
    vTaskDelete(NULL);
}

/**
 * @brief WebSocket 握手 handler
 */
static esp_err_t ws_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "WS handshake request from %s", req->uri);
    
    // 获取 Upgrade 头
    char upgrade[32] = {0};
    httpd_req_get_hdr_value_str(req, "Upgrade", upgrade, sizeof(upgrade));
    
    if (strcasecmp(upgrade, "websocket") != 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected WebSocket Upgrade");
        return ESP_FAIL;
    }
    
    // 获取 Sec-WebSocket-Key
    char ws_key[128] = {0};
    httpd_req_get_hdr_value_str(req, "Sec-WebSocket-Key", ws_key, sizeof(ws_key));
    
    if (strlen(ws_key) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing Sec-WebSocket-Key");
        return ESP_FAIL;
    }
    
    // 计算 Sec-WebSocket-Accept
    char accept_key[64] = {0};
    compute_accept_key(ws_key, accept_key, sizeof(accept_key));
    ESP_LOGI(TAG, "WS key: %s, Accept: %s", ws_key, accept_key);
    
    // 发送握手响应
    httpd_resp_set_status(req, "101 Switching Protocols");
    httpd_resp_set_type(req, "Upgrade");
    httpd_resp_set_hdr(req, "Upgrade", "websocket");
    httpd_resp_set_hdr(req, "Connection", "Upgrade");
    httpd_resp_set_hdr(req, "Sec-WebSocket-Accept", accept_key);
    httpd_resp_set_hdr(req, "Sec-WebSocket-Version", "13");
    
    esp_err_t err = httpd_resp_send(req, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Handshake response failed: %s", esp_err_to_name(err));
        return err;
    }
    
    // 获取 socket
    int sockfd = httpd_req_to_sockfd(req);
    
    // 添加客户端
    if (add_client(sockfd) < 0) {
        ESP_LOGE(TAG, "Max clients reached");
        close(sockfd);
        return ESP_FAIL;
    }
    
    // 创建处理任务
    xTaskCreate(ws_handler_task, "ws_handler", s_config.task_stack_size,
                (void*)sockfd, s_config.task_priority, NULL);
    
    return ESP_OK;
}

esp_err_t websocket_init(httpd_handle_t server, const ws_config_t *config)
{
    if (server == NULL || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 复制配置
    memcpy(&s_config, config, sizeof(ws_config_t));
    
    // 创建互斥锁
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    // 初始化客户端表
    memset(s_clients, 0, sizeof(s_clients));
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        s_clients[i].sockfd = -1;
    }
    
    // 设置默认栈大小和优先级
    if (s_config.task_stack_size == 0) {
        s_config.task_stack_size = 4096;
    }
    if (s_config.task_priority == 0) {
        s_config.task_priority = 5;
    }
    
    // 注册 URI handler
    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = NULL
    };
    
    esp_err_t err = httpd_register_uri_handler(server, &ws_uri);
    if (err != ESP_OK) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return err;
    }
    
    ESP_LOGI(TAG, "WebSocket server initialized");
    return ESP_OK;
}

esp_err_t websocket_send(int sockfd, const uint8_t *data, size_t len, ws_frame_type_t type)
{
    if (!websocket_is_connected(sockfd)) {
        return ESP_ERR_INVALID_STATE;
    }
    
    uint8_t frame[WS_BUF_SIZE + 16];
    uint8_t opcode;
    
    switch (type) {
        case WS_FRAME_TEXT: opcode = WS_OP_TEXT; break;
        case WS_FRAME_BINARY: opcode = WS_OP_BIN; break;
        case WS_FRAME_CLOSE: opcode = WS_OP_CLOSE; break;
        case WS_FRAME_PING: opcode = WS_OP_PING; break;
        case WS_FRAME_PONG: opcode = WS_OP_PONG; break;
        default: return ESP_ERR_INVALID_ARG;
    }
    
    size_t frame_len = encode_ws_frame(frame, data, len, opcode);
    
    int sent = send(sockfd, frame, frame_len, 0);
    if (sent < 0) {
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

esp_err_t websocket_send_text(int sockfd, const char *text)
{
    return websocket_send(sockfd, (const uint8_t*)text, strlen(text), WS_FRAME_TEXT);
}

esp_err_t websocket_broadcast(const uint8_t *data, size_t len, ws_frame_type_t type)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_clients[i].active) {
            websocket_send(s_clients[i].sockfd, data, len, type);
        }
    }
    
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t websocket_broadcast_text(const char *text)
{
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return websocket_broadcast((const uint8_t*)text, strlen(text), WS_FRAME_TEXT);
}

esp_err_t websocket_close(int sockfd)
{
    uint8_t close_frame[4] = {0x88, 0x02, 0x03, 0xE8}; // Close with code 1000
    send(sockfd, close_frame, 4, 0);
    
    remove_client(sockfd);
    close(sockfd);
    
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
    return get_client_index(sockfd) >= 0;
}
