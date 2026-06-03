/**
 * @file ws_client_mgr.c
 * @brief WebSocket client manager implementation
 *
 * Manages all WebSocket client connections including:
 * - Client state tracking
 * - Connect/disconnect handling
 * - Message queue management
 */

#include "ws_client_mgr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "WS_CLIENT_MGR";

esp_err_t ws_client_mgr_init(ws_client_mgr_t *mgr, const ws_client_mgr_config_t *config)
{
    if (mgr == NULL || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Clamp queue size to static storage limit (Fix #17)
    if (config->msg_queue_size > WS_MAX_MSG_QUEUE_SIZE) {
        ESP_LOGW(TAG, "msg_queue_size %d exceeds max %d, clamping",
                 config->msg_queue_size, WS_MAX_MSG_QUEUE_SIZE);
    }

    memset(mgr, 0, sizeof(ws_client_mgr_t));

    mgr->config.max_clients = config->max_clients;
    mgr->config.msg_queue_size = (config->msg_queue_size > WS_MAX_MSG_QUEUE_SIZE)
                                  ? WS_MAX_MSG_QUEUE_SIZE : config->msg_queue_size;
    mgr->config.max_msg_size = config->max_msg_size;
    mgr->max_clients = config->max_clients;

    mgr->clients = calloc(config->max_clients, sizeof(ws_client_t));
    if (mgr->clients == NULL) {
        ESP_LOGE(TAG, "Failed to allocate client array");
        return ESP_ERR_NO_MEM;
    }

    mgr->mutex = xSemaphoreCreateMutexStatic(&mgr->mutex_buffer);
    if (mgr->mutex == NULL) {
        free(mgr->clients);
        mgr->clients = NULL;
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

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

    xQueueReset(client->msg_queue);

    xSemaphoreGive(mgr->mutex);

    ESP_LOGI(TAG, "Client added: fd=%d, slot=%d, ip=%s, conn_id=%lu",
             fd, idx, client->client_ip, (unsigned long)client->connection_id);

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

    client->active = false;
    client->state = WS_CLIENT_STATE_DISCONNECTED;
    client->fd = -1;

    ws_msg_queue_item_t msg;
    while (xQueueReceive(client->msg_queue, &msg, 0) == pdTRUE) {
    }

    mgr->total_disconnections++;

    ESP_LOGI(TAG, "Client removed: fd=%d, slot=%d, conn_id=%lu, sent=%lu, recv=%lu",
             fd, idx, (unsigned long)client->connection_id,
             (unsigned long)client->msg_count_sent, (unsigned long)client->msg_count_received);

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
    xSemaphoreTake(mgr->mutex, portMAX_DELAY);
    for (int i = 0; i < mgr->max_clients; i++) {
        if (mgr->clients[i].active) {
            count++;
        }
    }
    xSemaphoreGive(mgr->mutex);
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

    // Pre-allocate array for deferred close actions
    #define MAX_TIMEOUT_ACTIONS 16
    struct { int fd; int slot; } close_actions[MAX_TIMEOUT_ACTIONS];
    int close_count = 0;
    int removed = 0;
    TickType_t now = xTaskGetTickCount();

    // Phase 1: Collect timed-out clients while holding lock
    xSemaphoreTake(mgr->mutex, portMAX_DELAY);

    for (int i = 0; i < mgr->max_clients && close_count < MAX_TIMEOUT_ACTIONS; i++) {
        if (mgr->clients[i].active) {
            TickType_t elapsed = now - mgr->clients[i].last_activity;
            if (elapsed > timeout_ticks) {
                ESP_LOGW(TAG, "Client timeout: fd=%d, slot=%d, ip=%s, idle=%lus",
                         mgr->clients[i].fd, i, mgr->clients[i].client_ip,
                         (unsigned long)(elapsed * portTICK_PERIOD_MS) / 1000);

                close_actions[close_count].fd = mgr->clients[i].fd;
                close_actions[close_count].slot = i;
                close_count++;

                // Mark inactive immediately while holding lock
                mgr->clients[i].active = false;
                mgr->clients[i].state = WS_CLIENT_STATE_DISCONNECTED;
                mgr->clients[i].fd = -1;
                removed++;
                mgr->total_disconnections++;
            }
        }
    }

    xSemaphoreGive(mgr->mutex);

    // Phase 2: Send close frames without holding lock
    for (int i = 0; i < close_count; i++) {
        httpd_ws_frame_t close_pkt = {
            .type = HTTPD_WS_TYPE_CLOSE,
            .payload = NULL,
            .len = 0,
        };
        httpd_ws_send_frame_async(server, close_actions[i].fd, &close_pkt);
    }

    return removed;
}

int ws_client_mgr_broadcast(ws_client_mgr_t *mgr, httpd_handle_t server,
                            const uint8_t *data, size_t len, httpd_ws_type_t type)
{
    if (mgr == NULL || server == NULL || data == NULL || len == 0) return 0;

    int fds[16];  // max clients to broadcast
    int fd_count = 0;

    // Phase 1: Collect active fds while holding lock
    xSemaphoreTake(mgr->mutex, portMAX_DELAY);
    for (int i = 0; i < mgr->max_clients && fd_count < 16; i++) {
        if (mgr->clients[i].active) {
            fds[fd_count++] = mgr->clients[i].fd;
        }
    }
    xSemaphoreGive(mgr->mutex);

    // Phase 2: Send without holding lock
    int success_count = 0;
    for (int i = 0; i < fd_count; i++) {
        httpd_ws_frame_t ws_frame = {
            .type = type,
            .payload = (uint8_t *)data,
            .len = len,
        };
        esp_err_t ret = httpd_ws_send_frame_async(server, fds[i], &ws_frame);
        if (ret == ESP_OK) {
            success_count++;
        }
        // Don't remove failed clients - let heartbeat timeout handle dead connections
        // This prevents disconnecting due to temporary send failures
    }

    return success_count;
}

esp_err_t ws_client_mgr_send_async(ws_client_mgr_t *mgr, httpd_handle_t server,
                                    int fd, const uint8_t *data, size_t len, httpd_ws_type_t type)
{
    if (mgr == NULL || server == NULL || fd < 0 || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (len > mgr->config.max_msg_size) {
        ESP_LOGW(TAG, "Message too large: %lu > %u",
                 (unsigned long)len, (unsigned int)mgr->config.max_msg_size);
        return ESP_ERR_INVALID_SIZE;
    }

    xSemaphoreTake(mgr->mutex, portMAX_DELAY);

    int idx = ws_client_mgr_find_by_fd(mgr, fd);
    if (idx < 0 || !mgr->clients[idx].active) {
        xSemaphoreGive(mgr->mutex);
        return ESP_ERR_INVALID_STATE;
    }

    ws_msg_queue_item_t msg = {
        .type = type,
        .len = len
    };
    memcpy(msg.payload, data, len);

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
    ESP_LOGI(TAG, "Total connections: %lu", (unsigned long)mgr->total_connections);
    ESP_LOGI(TAG, "Total disconnections: %lu", (unsigned long)mgr->total_disconnections);
    ESP_LOGI(TAG, "Total messages sent: %lu", (unsigned long)mgr->total_messages_sent);
    ESP_LOGI(TAG, "Total messages failed: %lu", (unsigned long)mgr->total_messages_failed);
    ESP_LOGI(TAG, "----------------------------------------");

    for (int i = 0; i < mgr->max_clients; i++) {
        const ws_client_t *client = &mgr->clients[i];
        ESP_LOGI(TAG, "Slot %d: %s fd=%d ip=%s state=%d idle=%lus sent=%lu recv=%lu err=%lu",
                 i,
                 client->active ? "[ACTIVE]" : "[FREE ]",
                 client->fd,
                 client->client_ip,
                 client->state,
                 client->active ? (unsigned long)((xTaskGetTickCount() - client->last_activity) * portTICK_PERIOD_MS) / 1000 : 0,
                 (unsigned long)client->msg_count_sent,
                 (unsigned long)client->msg_count_received,
                 (unsigned long)client->error_count);
    }

    ESP_LOGI(TAG, "========================================");

    xSemaphoreGive(mgr->mutex);
}
