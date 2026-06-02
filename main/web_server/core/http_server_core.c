/**
 * @file http_server_core.c
 * @brief HTTP server core implementation
 */

#include "http_server_core.h"
#include "server_config.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "HTTP_SERVER_CORE";

struct http_server {
    httpd_handle_t handle;
    server_config_t config;
    bool running;
    uint32_t start_time;

    uint32_t stats_total_requests;
    uint32_t stats_active_requests;
    uint32_t stats_error_requests;
    uint32_t stats_bytes_sent;
    uint32_t stats_bytes_received;

    bool graceful_shutdown;
    uint32_t shutdown_deadline;
};

http_server_t* http_server_create(const server_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Config is NULL");
        return NULL;
    }

    http_server_t *server = calloc(1, sizeof(http_server_t));
    if (server == NULL) {
        ESP_LOGE(TAG, "Failed to allocate server");
        return NULL;
    }

    memcpy(&server->config, config, sizeof(server_config_t));
    server->running = false;
    server->start_time = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;

    ESP_LOGI(TAG, "HTTP server created: port=%d, stack=%lu",
             config->http_port, (unsigned long)config->http_stack_size);

    return server;
}

esp_err_t http_server_core_start(http_server_t *server)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (server->running) {
        ESP_LOGW(TAG, "Server already running");
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.server_port = server->config.http_port;
    config.stack_size = server->config.http_stack_size > 0 ? server->config.http_stack_size : config.stack_size;
    config.max_uri_handlers = server->config.http_max_uri_handlers > 0 ? server->config.http_max_uri_handlers : config.max_uri_handlers;
    config.max_open_sockets = 20;  // 增加 socket 数量，避免并发加载时耗尽
    config.recv_wait_timeout = 3;   // 缩短接收超时，快速释放 socket
    config.send_wait_timeout = 3;   // 缩短发送超时
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;  // Enable wildcard URI matching for /*

    ESP_LOGI(TAG, "Starting HTTP server on port %d...", config.server_port);

    esp_err_t err = httpd_start(&server->handle, &config);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    server->running = true;
    server->graceful_shutdown = false;

    ESP_LOGI(TAG, "HTTP server started successfully");
    ESP_LOGI(TAG, "  - Server port: %d", config.server_port);
    ESP_LOGI(TAG, "  - Stack size: %d", config.stack_size);
    ESP_LOGI(TAG, "  - Max handlers: %d", config.max_uri_handlers);
    ESP_LOGI(TAG, "  - Max sockets: %d", config.max_open_sockets);

    return ESP_OK;
}

esp_err_t http_server_core_stop(http_server_t *server)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!server->running) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping HTTP server...");

    esp_err_t err = httpd_stop(server->handle);

    if (err == ESP_OK) {
        server->handle = NULL;
        server->running = false;
        ESP_LOGI(TAG, "HTTP server stopped");
    } else {
        ESP_LOGE(TAG, "Failed to stop HTTP server: %s", esp_err_to_name(err));
    }

    return err;
}

esp_err_t http_server_core_graceful_stop(http_server_t *server, uint32_t timeout_ms)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!server->running) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Graceful shutdown initiated (timeout=%ums)...", (unsigned int)timeout_ms);

    server->graceful_shutdown = true;
    server->shutdown_deadline = xTaskGetTickCount() + (timeout_ms / portTICK_PERIOD_MS);

    uint32_t waited = 0;
    while (server->stats_active_requests > 0 && waited < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(100));
        waited += 100;
    }

    if (server->stats_active_requests > 0) {
        ESP_LOGW(TAG, "Graceful shutdown timeout, %lu requests still active",
                 (unsigned long)server->stats_active_requests);
    }

    return http_server_core_stop(server);
}

void http_server_core_destroy(http_server_t *server)
{
    if (server == NULL) {
        return;
    }

    if (server->running) {
        http_server_core_stop(server);
    }

    free(server);
    ESP_LOGI(TAG, "HTTP server destroyed");
}

httpd_handle_t http_server_core_get_handle(http_server_t *server)
{
    if (server == NULL) {
        return NULL;
    }
    return server->handle;
}

bool http_server_core_is_running(http_server_t *server)
{
    if (server == NULL) {
        return false;
    }
    return server->running;
}

void http_server_core_get_stats(http_server_t *server,
                          uint32_t *total_requests,
                          uint32_t *active_requests,
                          uint32_t *error_requests)
{
    if (server == NULL) {
        return;
    }

    if (total_requests) *total_requests = server->stats_total_requests;
    if (active_requests) *active_requests = server->stats_active_requests;
    if (error_requests) *error_requests = server->stats_error_requests;
}
