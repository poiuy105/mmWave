/**
 * @file ws_heartbeat.c
 * @brief WebSocket 蹇冭烦妫€娴嬪疄鐜? */

#include "ws_heartbeat.h"
#include "esp_log.h"

static const char *TAG = "WS_HEARTBEAT";

static void ws_heartbeat_task(void *arg)
{
    ws_heartbeat_ctx_t *ctx = (ws_heartbeat_ctx_t *)arg;
    TickType_t last_wake = xTaskGetTickCount();

    ESP_LOGI(TAG, "Heartbeat task started: interval=%u us, timeout=%u us",
             (unsigned int)ctx->config.check_interval_sec,
             (unsigned int)ctx->config.client_timeout_sec);

    while (ctx->running) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(ctx->config.check_interval_sec * 1000));

        if (!ctx->running) {
            break;
        }

        if (ctx->client_mgr == NULL || ctx->http_server == NULL) {
            continue;
        }

        TickType_t now = xTaskGetTickCount();
        TickType_t timeout_ticks = pdMS_TO_TICKS(ctx->config.client_timeout_sec * 1000);
        TickType_t ping_threshold = pdMS_TO_TICKS((ctx->config.check_interval_sec + 5) * 1000);

        xSemaphoreTake(ctx->client_mgr->mutex, portMAX_DELAY);

        for (int i = 0; i < ctx->client_mgr->max_clients; i++) {
            ws_client_t *client = &ctx->client_mgr->clients[i];

            if (!client->active) {
                continue;
            }

            TickType_t idle_time = now - client->last_activity;

            if (idle_time > timeout_ticks) {
                ESP_LOGW(TAG, "Client timeout: fd=%d, ip=%s, idle=%lus",
                         client->fd, client->client_ip,
                         (unsigned long)(idle_time * portTICK_PERIOD_MS) / 1000);

                httpd_ws_frame_t close_pkt = {
                    .type = HTTPD_WS_TYPE_CLOSE,
                    .payload = NULL,
                    .len = 0,
                };
                httpd_ws_send_frame_async(ctx->http_server, client->fd, &close_pkt);

                client->active = false;
                client->state = WS_CLIENT_STATE_DISCONNECTED;
                int old_fd = client->fd;
                client->fd = -1;
                ctx->client_mgr->total_disconnections++;
                ctx->total_timeouts++;

                ESP_LOGI(TAG, "Client disconnected due to timeout: old_fd=%d", old_fd);
            }
            else if (ctx->config.auto_ping && idle_time > ping_threshold) {
                httpd_ws_frame_t ping_pkt = {
                    .type = HTTPD_WS_TYPE_PING,
                    .payload = NULL,
                    .len = 0,
                };

                esp_err_t ret = httpd_ws_send_frame_async(
                    ctx->http_server, client->fd, &ping_pkt);

                if (ret == ESP_OK) {
                    client->state = WS_CLIENT_STATE_PING_PENDING;
                    ctx->total_pings_sent++;
                }
            }
        }

        xSemaphoreGive(ctx->client_mgr->mutex);
    }

    ESP_LOGI(TAG, "Heartbeat task stopped");
    vTaskDelete(NULL);
}

esp_err_t ws_heartbeat_init(ws_heartbeat_ctx_t *ctx,
                            ws_client_mgr_t *client_mgr,
                            httpd_handle_t http_server,
                            const ws_heartbeat_config_t *config)
{
    if (ctx == NULL || client_mgr == NULL || http_server == NULL || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(ctx, 0, sizeof(ws_heartbeat_ctx_t));

    ctx->client_mgr = client_mgr;
    ctx->http_server = http_server;
    ctx->config = *config;
    ctx->running = false;

    ESP_LOGI(TAG, "Heartbeat module initialized: interval=%u us, timeout=%u us",
             (unsigned int)config->check_interval_sec,
             (unsigned int)config->client_timeout_sec);

    return ESP_OK;
}

esp_err_t ws_heartbeat_start(ws_heartbeat_ctx_t *ctx)
{
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (ctx->running) {
        ESP_LOGW(TAG, "Heartbeat already running");
        return ESP_OK;
    }

    ctx->running = true;

    BaseType_t ret = xTaskCreate(
        ws_heartbeat_task,
        "ws_heartbeat",
        4096,
        ctx,
        3,
        &ctx->task_handle
    );

    if (ret != pdPASS) {
        ctx->running = false;
        ESP_LOGE(TAG, "Failed to create heartbeat task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Heartbeat task started");
    return ESP_OK;
}

esp_err_t ws_heartbeat_stop(ws_heartbeat_ctx_t *ctx)
{
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!ctx->running) {
        return ESP_OK;
    }

    ctx->running = false;

    if (ctx->task_handle) {
        vTaskDelay(pdMS_TO_TICKS(100));
        ctx->task_handle = NULL;
    }

    ESP_LOGI(TAG, "Heartbeat stopped");
    return ESP_OK;
}

void ws_heartbeat_deinit(ws_heartbeat_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    ws_heartbeat_stop(ctx);
    memset(ctx, 0, sizeof(ws_heartbeat_ctx_t));
}

esp_err_t ws_heartbeat_send_ping(httpd_handle_t http_server, int fd)
{
    if (http_server == NULL || fd < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    httpd_ws_frame_t ping_pkt = {
        .type = HTTPD_WS_TYPE_PING,
        .payload = NULL,
        .len = 0,
    };

    return httpd_ws_send_frame_async(http_server, fd, &ping_pkt);
}

void ws_heartbeat_get_stats(const ws_heartbeat_ctx_t *ctx,
                           uint32_t *pings_sent, uint32_t *timeouts, uint32_t *pongs_received)
{
    if (ctx == NULL) {
        return;
    }

    if (pings_sent) *pings_sent = ctx->total_pings_sent;
    if (timeouts) *timeouts = ctx->total_timeouts;
    if (pongs_received) *pongs_received = ctx->total_pong_received;
}
