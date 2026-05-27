/**
 * @file server_context.c
 * @brief Server context management
 */

#include "server_context.h"
#include "server_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "SERVER_CONTEXT";

static server_context_t *s_ctx = NULL;
static StaticSemaphore_t s_ctx_mutex_buffer;
static StaticSemaphore_t s_stats_mutex_buffer;

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

    // Create mutexes
    s_ctx->mutex = xSemaphoreCreateMutexStatic(&s_ctx_mutex_buffer);
    s_ctx->stats_mutex = xSemaphoreCreateMutexStatic(&s_stats_mutex_buffer);

    if (s_ctx->mutex == NULL || s_ctx->stats_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutexes");
        free(s_ctx);
        s_ctx = NULL;
        return ESP_ERR_NO_MEM;
    }

    // Make a copy of config (Fix #16: avoid dangling pointer to stack variable)
    s_ctx->config = calloc(1, sizeof(server_config_t));
    if (s_ctx->config == NULL) {
        ESP_LOGE(TAG, "Failed to allocate config copy");
        free(s_ctx);
        s_ctx = NULL;
        return ESP_ERR_NO_MEM;
    }
    memcpy(s_ctx->config, config, sizeof(server_config_t));
    s_ctx->state = SERVER_STATE_INITIALIZED;
    s_ctx->graceful_shutdown_pending = false;
    s_ctx->start_time = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;

    // Initialize stats
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

    if (s_ctx->config) {
        free(s_ctx->config);
        s_ctx->config = NULL;
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
    // Memory barrier to ensure pointer read is not reordered
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return s_ctx;
}

void server_stats_inc_request(void)
{
    if (s_ctx && s_ctx->stats_mutex) {
        xSemaphoreTake(s_ctx->stats_mutex, portMAX_DELAY);
        s_ctx->stats.total_requests++;
        s_ctx->stats.active_requests++;
        xSemaphoreGive(s_ctx->stats_mutex);
    }
}

void server_stats_dec_request(void)
{
    if (s_ctx && s_ctx->stats_mutex) {
        xSemaphoreTake(s_ctx->stats_mutex, portMAX_DELAY);
        if (s_ctx->stats.active_requests > 0) {
            s_ctx->stats.active_requests--;
        }
        xSemaphoreGive(s_ctx->stats_mutex);
    }
}

void server_stats_inc_error(void)
{
    if (s_ctx && s_ctx->stats_mutex) {
        xSemaphoreTake(s_ctx->stats_mutex, portMAX_DELAY);
        s_ctx->stats.error_requests++;
        xSemaphoreGive(s_ctx->stats_mutex);
    }
}

void server_stats_inc_ws_connection(void)
{
    if (s_ctx && s_ctx->stats_mutex) {
        xSemaphoreTake(s_ctx->stats_mutex, portMAX_DELAY);
        s_ctx->stats.ws_connections++;
        xSemaphoreGive(s_ctx->stats_mutex);
    }
}

void server_stats_inc_ws_disconnect(void)
{
    if (s_ctx && s_ctx->stats_mutex) {
        xSemaphoreTake(s_ctx->stats_mutex, portMAX_DELAY);
        s_ctx->stats.ws_disconnections++;
        xSemaphoreGive(s_ctx->stats_mutex);
    }
}

void server_stats_inc_ws_message_sent(void)
{
    if (s_ctx && s_ctx->stats_mutex) {
        xSemaphoreTake(s_ctx->stats_mutex, portMAX_DELAY);
        s_ctx->stats.ws_messages_sent++;
        xSemaphoreGive(s_ctx->stats_mutex);
    }
}

void server_stats_inc_ws_message_failed(void)
{
    if (s_ctx && s_ctx->stats_mutex) {
        xSemaphoreTake(s_ctx->stats_mutex, portMAX_DELAY);
        s_ctx->stats.ws_messages_failed++;
        xSemaphoreGive(s_ctx->stats_mutex);
    }
}

void server_stats_inc_rate_limit(void)
{
    if (s_ctx && s_ctx->stats_mutex) {
        xSemaphoreTake(s_ctx->stats_mutex, portMAX_DELAY);
        s_ctx->stats.rate_limit_hits++;
        xSemaphoreGive(s_ctx->stats_mutex);
    }
}

void server_stats_inc_validation_failure(void)
{
    if (s_ctx && s_ctx->stats_mutex) {
        xSemaphoreTake(s_ctx->stats_mutex, portMAX_DELAY);
        s_ctx->stats.validation_failures++;
        xSemaphoreGive(s_ctx->stats_mutex);
    }
}

void server_stats_inc_timeout(void)
{
    if (s_ctx && s_ctx->stats_mutex) {
        xSemaphoreTake(s_ctx->stats_mutex, portMAX_DELAY);
        s_ctx->stats.timeout_errors++;
        xSemaphoreGive(s_ctx->stats_mutex);
    }
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
