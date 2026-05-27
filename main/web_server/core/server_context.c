/**
 * @file server_context.c
 * @brief 服务器上下文实现
 */

#include "server_context.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "SERVER_CONTEXT";

static server_context_t *s_ctx = NULL;
static StaticSemaphore_t s_ctx_mutex_buffer;
static StaticSemaphore_t s_stats_mutex_buffer;

static void stats_updater_task(void *arg)
{
    uint32_t last_update = 0;

    while (server_is_running()) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        server_context_t *ctx = server_context_get();
        if (ctx == NULL) continue;

        // 每秒更新运行时间
        if (xTaskGetTickCount() / configTICK_RATE_HZ > last_update) {
            last_update = xTaskGetTickCount() / configTICK_RATE_HZ;
            ctx->stats.uptime_seconds++;
            ctx->stats.free_heap_current = esp_get_free_heap_size();

            if (ctx->stats.free_heap_min == 0) {
                ctx->stats.free_heap_min = ctx->stats.free_heap_current;
            }
        }
    }

    vTaskDelete(NULL);
}

esp_err_t server_context_init(const struct server_config *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_ctx != NULL) {
        ESP_LOGW(TAG, "Context already initialized");
        return ESP_OK;
    }

    s_ctx = calloc(1, sizeof(server_context_t));
    if (s_ctx == NULL) {
        ESP_LOGE(TAG, "Failed to allocate context");
        return ESP_ERR_NO_MEM;
    }

    // 创建互斥量
    s_ctx->mutex = xSemaphoreCreateMutexStatic(&s_ctx_mutex_buffer);
    s_ctx->stats_mutex = xSemaphoreCreateMutexStatic(&s_stats_mutex_buffer);

    if (s_ctx->mutex == NULL || s_ctx->stats_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutexes");
        free(s_ctx);
        s_ctx = NULL;
        return ESP_ERR_NO_MEM;
    }

    // 初始化状态
    s_ctx->config = config;
    s_ctx->state = SERVER_STATE_INITIALIZED;
    s_ctx->graceful_shutdown_pending = false;
    s_ctx->start_time = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;

    // 初始化统计
    memset(&s_ctx->stats, 0, sizeof(server_stats_t));
    s_ctx->stats.free_heap_min = esp_get_free_heap_size();

    ESP_LOGI(TAG, "Server context initialized");

    return ESP_OK;
}

esp_err_t server_context_deinit(void)
{
    if (s_ctx == NULL) {
        return ESP_OK;
    }

    if (s_ctx->state == SERVER_STATE_RUNNING) {
        s_ctx->state = SERVER_STATE_STOPPED;
    }

    if (s_ctx->stats_mutex) {
        vSemaphoreDelete(s_ctx->stats_mutex);
    }

    if (s_ctx->mutex) {
        vSemaphoreDelete(s_ctx->mutex);
    }

    free(s_ctx);
    s_ctx = NULL;

    ESP_LOGI(TAG, "Server context deinitialized");

    return ESP_OK;
}

server_context_t* server_context_get(void)
{
    return s_ctx;
}

// 统计更新宏（线程安全）
#define STATS_INC(counter) \
    do { \
        if (s_ctx && s_ctx->stats_mutex) { \
            xSemaphoreTake(s_ctx->stats_mutex, portMAX_DELAY); \
            s_ctx->stats.counter++; \
            xSemaphoreGive(s_ctx->stats_mutex); \
        } \
    } while(0)

void server_stats_inc_request(void)
{
    STATS_INC(total_requests);
}

void server_stats_dec_request(void)
{
    STATS_INC(active_requests);
    if (s_ctx && s_ctx->stats.active_requests > 0) {
        xSemaphoreTake(s_ctx->stats_mutex, portMAX_DELAY);
        if (s_ctx->stats.active_requests > 0) {
            s_ctx->stats.active_requests--;
        }
        xSemaphoreGive(s_ctx->stats_mutex);
    }
}

void server_stats_inc_error(void)
{
    STATS_INC(error_requests);
}

void server_stats_inc_ws_connection(void)
{
    STATS_INC(ws_connections);
}

void server_stats_inc_ws_disconnect(void)
{
    STATS_INC(ws_disconnections);
}

void server_stats_inc_ws_message_sent(void)
{
    STATS_INC(ws_messages_sent);
}

void server_stats_inc_ws_message_failed(void)
{
    STATS_INC(ws_messages_failed);
}

void server_stats_inc_rate_limit(void)
{
    STATS_INC(rate_limit_hits);
}

void server_stats_inc_validation_failure(void)
{
    STATS_INC(validation_failures);
}

void server_stats_inc_timeout(void)
{
    STATS_INC(timeout_errors);
}

void server_stats_update_heap(void)
{
    if (s_ctx && s_ctx->stats_mutex) {
        xSemaphoreTake(s_ctx->stats_mutex, portMAX_DELAY);
        s_ctx->stats.free_heap_current = esp_get_free_heap_size();
        if (s_ctx->stats.free_heap_current < s_ctx->stats.free_heap_min) {
            s_ctx->stats.free_heap_min = s_ctx->stats.free_heap_current;
        }
        xSemaphoreGive(s_ctx->stats_mutex);
    }
}

void server_stats_get_copy(server_stats_t *stats)
{
    if (stats == NULL || s_ctx == NULL) {
        return;
    }

    if (s_ctx->stats_mutex) {
        xSemaphoreTake(s_ctx->stats_mutex, portMAX_DELAY);
        *stats = s_ctx->stats;
        xSemaphoreGive(s_ctx->stats_mutex);
    } else {
        *stats = s_ctx->stats;
    }
}
