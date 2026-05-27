/**
 * @file ws_client_mgr.c
 * @brief WebSocket 客户端管理器实现
 */

#include "ws_client_mgr.h"
#include "esp_log.h"
#include "esp_err.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "WS_CLIENT_MGR";

esp_err_t ws_client_mgr_init(ws_client_mgr_t *mgr, const ws_client_mgr_config_t *config)
{
    if (mgr == NULL || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(mgr, 0, sizeof(ws_client_mgr_t));

    // 保存配置
    mgr->config.max_clients = config->max_clients;
    mgr->config.msg_queue_size = config->msg_queue_size;
    mgr->config.max_msg_size = config->max_msg_size;
    mgr->max_clients = config->max_clients;

    // 分配客户端数组
    mgr->clients = calloc(config->max_clients, sizeof(ws_client_t));
    if (mgr->clients == NULL) {
        ESP_LOGE(TAG, "Failed to allocate client array");
        return ESP_ERR_NO_MEM;
    }

    // 初始化互斥量
    mgr->mutex = xSemaphoreCreateMutexStatic(&mgr->mutex_buffer);
    if (mgr->mutex == NULL) {
        free(mgr->clients);
        mgr->clients = NULL;
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // 初始化客户端
    for (int i = 0; i < mgr->max_clients; i++) {
        mgr->clients[i].fd = -1;
        mgr->clients[i].state = WS_CLIENT_STATE_IDLE;
        mgr->clients[i].msg_queue = xQueueCreateStatic(
            config->msg_queue_size,
            sizeof(ws_msg_queue_item_t),
            mgr->clients[i].msg_queue_storage,
            &mgr->clients[i].msg_queue_buffer
        );
    }

    ESP_LOGI(TAG, "Client manager initialized: max_clients=%d, queue_size=%d",
             config->max_clients, config->msg_queue_size);

    return ESP_OK;
}

void ws_client_mgr_deinit(ws_client_mgr_t *mgr)
{
    if (mgr == NULL) return;

    xSemaphoreTake(mgr->mutex, portMAX_DELAY);

    // 清理所有客户端
    for (int i = 0; i < mgr->max_clients; i++) {
        if (mgr->clients[i].msg_queue) {
            vQueueDelete(mgr->clients[i].msg_queue);
            mgr->clients[i].msg_queue = NULL;
        }
    }

    if (mgr->clients) {
        free(mgr->clients);
        mgr->clients = NULL;
    }

    xSemaphoreGive(mgr->mutex);
    vSemaphoreDelete(mgr->mutex);
    mgr->mutex = NULL;

    ESP_LOGI(TAG, "Client manager deinitialized");
}

int ws_client_mgr_add(ws_client_mgr_t *mgr, int fd, const char *client_ip)
{
    if (mgr == NULL || fd < 0) {
        return -1;
    }

    xSemaphoreTake(mgr->mutex, portMAX_DELAY);

    int idx = -1;
    for (int i = 0; i < mgr->max_clients; i++) {
        if (!mgr->clients[i].active) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        xSemaphoreGive(mgr->mutex);
        ESP_LOGW(TAG, "No free slot for new client fd=%d", fd);
        return -1;
    }

    // 初始化客户端
    ws_client_t *client = &mgr->clients[idx];
    client->fd = fd;
    client->active = true;
    client->state = WS_CLIENT_STATE_CONNECTED;
    client->last_activity = xTaskGetTickCount();
    client->connection_id = ++mgr->total_connections;
    client->msg_count_sent = 0;
    client->msg_count_received = 0;
    client->error_count = 0;
    client->bytes_sent = 0;
    client->bytes_received = 0;

    if (client_ip) {
        strncpy(client->client_ip, client_ip, WS_CLIENT_IP_LEN - 1);
        client->client_ip[WS_CLIENT_IP_LEN - 1] = '\0';
    } else {
        strcpy(client->client_ip, "unknown");
    }

    // 重置消息队列
    xQueueReset(client->msg_queue);

    xSemaphoreGive(mgr->mutex);

    ESP_LOGI(TAG, "Client added: fd=%d, slot=%d, ip=%s, conn_id=%lu",
             fd, idx, client->client_ip, client->connection_id);

    return idx;
}

void ws_client_mgr_remove(ws_client_mgr_t *mgr, int fd)
{
    if (mgr == NULL || fd < 0) {
        return;
    }

    xSemaphoreTake(mgr->mutex, portMAX_DELAY);

    int idx = ws_client_mgr_find_by_fd(mgr, fd);
    if (idx < 0) {
        xSemaphoreGive(mgr->mutex);
        return;
    }

    ws_client_t *client = &mgr->clients[idx];

    // 标记为非活跃
    client->active = false;
    client->state = WS_CLIENT_STATE_DISCONNECTED;
    client->fd = -1;

    // 清空消息队列
    ws_msg_queue_item_t msg;
    while (xQueueReceive(client->msg_queue, &msg, 0) == pdTRUE) {
        // 丢弃队列中的消息
    }

    mgr->total_disconnections++;

    ESP_LOGI(TAG, "Client removed: fd=%d, slot=%d, conn_id=%lu, sent=%lu, recv=%lu",
             fd, idx, client->connection_id,
             client->msg_count_sent, client->msg_count_received);

    xSemaphoreGive(mgr->mutex);
}

int ws_client_mgr_find_by_fd(ws_client_mgr_t *mgr, int fd)
{
    if (mgr == NULL || fd < 0) {
        return -1;
    }

    for (int i = 0; i < mgr->max_clients; i++) {
        if (mgr->clients[i].active && mgr->clients[i].fd == fd) {
            return i;
        }
    }
    return -1;
}

void ws_client_mgr_update_activity(ws_client_mgr_t *mgr, int fd)
{
    if (mgr == NULL || fd < 0) {
        return;
    }

    xSemaphoreTake(mgr->mutex, portMAX_DELAY);

    int idx = ws_client_mgr_find_by_fd(mgr, fd);
    if (idx >= 0) {
        mgr->clients[idx].last_activity = xTaskGetTickCount();
        if (mgr->clients[idx].state == WS_CLIENT_STATE_CONNECTED) {
            mgr->clients[idx].state = WS_CLIENT_STATE_ACTIVE;
        }
    }

    xSemaphoreGive(mgr->mutex);
}

int ws_client_mgr_get_active_count(const ws_client_mgr_t *mgr)
{
    if (mgr == NULL) {
        return 0;
    }

    int count = 0;
    for (int i = 0; i < mgr->max_clients; i++) {
        if (mgr->clients[i].active) {
            count++;
        }
    }
    return count;
}

bool ws_client_mgr_is_active(const ws_client_mgr_t *mgr, int fd)
{
    if (mgr == NULL || fd < 0) {
        return false;
    }

    bool active = false;
    xSemaphoreTake(mgr->mutex, portMAX_DELAY);

    int idx = ws_client_mgr_find_by_fd((ws_client_mgr_t *)mgr, fd);
    if (idx >= 0) {
        active = mgr->clients[idx].active;
    }

    xSemaphoreGive(mgr->mutex);
    return active;
}

int ws_client_mgr_remove_timeout(ws_client_mgr_t *mgr, httpd_handle_t server, TickType_t timeout_ticks)
{
    if (mgr == NULL || server == NULL) {
        return 0;
    }

    int removed = 0;
    TickType_t now = xTaskGetTickCount();

    xSemaphoreTake(mgr->mutex, portMAX_DELAY);

    for (int i = 0; i < mgr->max_clients; i++) {
        if (mgr->clients[i].active) {
            TickType_t elapsed = now - mgr->clients[i].last_activity;
            if (elapsed > timeout_ticks) {
                ESP_LOGW(TAG, "Client timeout: fd=%d, slot=%d, ip=%s, idle=%lus",
                         mgr->clients[i].fd, i, mgr->clients[i].client_ip,
                         (elapsed * portTICK_PERIOD_MS) / 1000);

                // 发送关闭帧
                httpd_ws_frame_t close_pkt = {
                    .type = HTTPD_WS_TYPE_CLOSE,
                    .payload = NULL,
                    .len = 0,
                };
                httpd_ws_send_frame_async(server, mgr->clients[i].fd, &close_pkt);

                // 标记移除
                mgr->clients[i].active = false;
                mgr->clients[i].state = WS_CLIENT_STATE_DISCONNECTED;
                mgr->clients[i].fd = -1;
                removed++;
                mgr->total_disconnections++;
            }
        }
    }

    xSemaphoreGive(mgr->mutex);
    return removed;
}

int ws_client_mgr_broadcast(ws_client_mgr_t *mgr, httpd_handle_t server,
                            const uint8_t *data, size_t len, httpd_ws_type_t type)
{
    if (mgr == NULL || server == NULL || data == NULL || len == 0) {
        return 0;
    }

    int success_count = 0;

    xSemaphoreTake(mgr->mutex, portMAX_DELAY);

    for (int i = 0; i < mgr->max_clients; i++) {
        if (mgr->clients[i].active) {
            esp_err_t ret = httpd_ws_send_frame_async(
                server,
                mgr->clients[i].fd,
                &(httpd_ws_frame_t){
                    .type = type,
                    .payload = (uint8_t *)data,
                    .len = len
                }
            );

            if (ret == ESP_OK) {
                success_count++;
                mgr->clients[i].msg_count_sent++;
                mgr->clients[i].bytes_sent += len;
            } else {
                ESP_LOGW(TAG, "Broadcast failed to fd=%d: %s",
                         mgr->clients[i].fd, esp_err_to_name(ret));
                mgr->clients[i].error_count++;
                mgr->clients[i].active = false;
                mgr->clients[i].fd = -1;
            }
        }
    }

    xSemaphoreGive(mgr->mutex);

    return success_count;
}

esp_err_t ws_client_mgr_send_async(ws_client_mgr_t *mgr, httpd_handle_t server,
                                    int fd, const uint8_t *data, size_t len, httpd_ws_type_t type)
{
    if (mgr == NULL || server == NULL || fd < 0 || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (len > mgr->config.max_msg_size) {
        ESP_LOGW(TAG, "Message too large: %zu > %d", len, mgr->config.max_msg_size);
        return ESP_ERR_INVALID_SIZE;
    }

    xSemaphoreTake(mgr->mutex, portMAX_DELAY);

    int idx = ws_client_mgr_find_by_fd(mgr, fd);
    if (idx < 0 || !mgr->clients[idx].active) {
        xSemaphoreGive(mgr->mutex);
        return ESP_ERR_INVALID_STATE;
    }

    // 准备消息
    ws_msg_queue_item_t msg = {
        .type = type,
        .len = len
    };
    memcpy(msg.payload, data, len);

    // 发送到队列（非阻塞）
    BaseType_t ret = xQueueSend(mgr->clients[idx].msg_queue, &msg, 0);

    xSemaphoreGive(mgr->mutex);

    if (ret != pdTRUE) {
        ESP_LOGW(TAG, "Queue full for fd=%d", fd);
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void ws_client_mgr_get_stats(const ws_client_mgr_t *mgr,
                             uint32_t *total_conn, uint32_t *total_disconn,
                             uint32_t *total_sent, uint32_t *total_failed,
                             int *active)
{
    if (mgr == NULL) {
        return;
    }

    xSemaphoreTake(mgr->mutex, portMAX_DELAY);

    if (total_conn) *total_conn = mgr->total_connections;
    if (total_disconn) *total_disconn = mgr->total_disconnections;
    if (total_sent) *total_sent = mgr->total_messages_sent;
    if (total_failed) *total_failed = mgr->total_messages_failed;
    if (active) *active = ws_client_mgr_get_active_count(mgr);

    xSemaphoreGive(mgr->mutex);
}

void ws_client_mgr_dump_status(const ws_client_mgr_t *mgr)
{
    if (mgr == NULL) return;

    xSemaphoreTake(mgr->mutex, portMAX_DELAY);

    ESP_LOGI(TAG, "=== WebSocket Client Manager Status ===");
    ESP_LOGI(TAG, "Max clients: %d", mgr->max_clients);
    ESP_LOGI(TAG, "Total connections: %lu", mgr->total_connections);
    ESP_LOGI(TAG, "Total disconnections: %lu", mgr->total_disconnections);
    ESP_LOGI(TAG, "Total messages sent: %lu", mgr->total_messages_sent);
    ESP_LOGI(TAG, "Total messages failed: %lu", mgr->total_messages_failed);
    ESP_LOGI(TAG, "----------------------------------------");

    for (int i = 0; i < mgr->max_clients; i++) {
        const ws_client_t *client = &mgr->clients[i];
        ESP_LOGI(TAG, "Slot %d: %s fd=%d ip=%s state=%d idle=%lus sent=%lu recv=%lu err=%lu",
                 i,
                 client->active ? "[ACTIVE]" : "[FREE ]",
                 client->fd,
                 client->client_ip,
                 client->state,
                 client->active ? ((xTaskGetTickCount() - client->last_activity) * portTICK_PERIOD_MS) / 1000 : 0,
                 client->msg_count_sent,
                 client->msg_count_received,
                 client->error_count);
    }

    ESP_LOGI(TAG, "========================================");

    xSemaphoreGive(mgr->mutex);
}
