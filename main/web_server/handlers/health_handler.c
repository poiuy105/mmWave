/**
 * @file health_handler.c
 * @brief 健康检查 API 实现
 */

#include "health_handler.h"
#include "server_context.h"
#include "ws_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <cJSON.h>

static const char *TAG = "HEALTH";

/**
 * @brief GET /api/health - 健康检查
 */
static esp_err_t api_health_handler(httpd_req_t *req)
{
    server_context_t *ctx = server_context_get();

    cJSON *root = cJSON_CreateObject();

    // 基本状态
    if (ctx && ctx->state == SERVER_STATE_RUNNING) {
        cJSON_AddStringToObject(root, "status", "healthy");
    } else {
        cJSON_AddStringToObject(root, "status", "unhealthy");
    }

    cJSON_AddStringToObject(root, "version", "2.0.0");
    cJSON_AddNumberToObject(root, "uptime", ctx ? ctx->stats.uptime_seconds : 0);

    // 内存状态
    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t min_free = esp_get_minimum_free_heap_size();
    cJSON_AddNumberToObject(root, "free_heap", free_heap);
    cJSON_AddNumberToObject(root, "min_free_heap", min_free);

    // 更新上下文中的最小堆
    if (ctx && free_heap < ctx->stats.free_heap_min) {
        ctx->stats.free_heap_min = free_heap;
    }

    // HTTP 统计
    cJSON *http = cJSON_CreateObject();
    cJSON_AddNumberToObject(http, "total_requests", ctx ? ctx->stats.total_requests : 0);
    cJSON_AddNumberToObject(http, "active_requests", ctx ? ctx->stats.active_requests : 0);
    cJSON_AddNumberToObject(http, "error_requests", ctx ? ctx->stats.error_requests : 0);
    if (ctx && ctx->stats.total_requests > 0) {
        float error_rate = (float)ctx->stats.error_requests / ctx->stats.total_requests;
        cJSON_AddNumberToObject(http, "error_rate", error_rate);
    } else {
        cJSON_AddNumberToObject(http, "error_rate", 0.0);
    }
    cJSON_AddItemToObject(root, "http", http);

    // WebSocket 统计
    cJSON *ws = cJSON_CreateObject();
    cJSON_AddNumberToObject(ws, "connected_clients", ctx ? ws_server_get_client_count(ctx->ws_server) : 0);
    cJSON_AddNumberToObject(ws, "total_connections", ctx ? ctx->stats.ws_connections : 0);
    cJSON_AddNumberToObject(ws, "total_disconnections", ctx ? ctx->stats.ws_disconnections : 0);
    cJSON_AddNumberToObject(ws, "messages_sent", ctx ? ctx->stats.ws_messages_sent : 0);
    cJSON_AddNumberToObject(ws, "messages_failed", ctx ? ctx->stats.ws_messages_failed : 0);
    cJSON_AddItemToObject(root, "websocket", ws);

    // 错误统计
    cJSON *errors = cJSON_CreateObject();
    cJSON_AddNumberToObject(errors, "rate_limit_hits", ctx ? ctx->stats.rate_limit_hits : 0);
    cJSON_AddNumberToObject(errors, "validation_failures", ctx ? ctx->stats.validation_failures : 0);
    cJSON_AddNumberToObject(errors, "timeout_errors", ctx ? ctx->stats.timeout_errors : 0);
    cJSON_AddItemToObject(root, "errors", errors);

    // 广播状态
    cJSON *broadcast = cJSON_CreateObject();
    cJSON_AddBoolToObject(broadcast, "enabled", ctx ? ctx->config->broadcast_enabled : false);
    cJSON_AddNumberToObject(broadcast, "interval", ctx ? ctx->config->broadcast_interval : 0);
    cJSON_AddItemToObject(root, "broadcast", broadcast);

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));

    free(json);
    cJSON_Delete(root);

    return ESP_OK;
}

/**
 * @brief GET /api/ready - 就绪检查（用于 K8s/Docker 健康探测）
 */
static esp_err_t api_ready_handler(httpd_req_t *req)
{
    server_context_t *ctx = server_context_get();

    // 检查服务器是否运行
    if (!ctx || ctx->state != SERVER_STATE_RUNNING) {
        httpd_resp_send_err(req, HTTPD_503_SERVICE_UNAVAILABLE, "Server not ready");
        return ESP_FAIL;
    }

    // 检查 FATFS 是否就绪
    // extern bool file_manager_is_ready(void);
    // if (!file_manager_is_ready()) {
    //     httpd_resp_send_err(req, HTTPD_503_SERVICE_UNAVAILABLE, "Storage not ready");
    //     return ESP_FAIL;
    // }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ready", true);
    cJSON_AddNumberToObject(root, "uptime", ctx->stats.uptime_seconds);

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));

    free(json);
    cJSON_Delete(root);

    return ESP_OK;
}

/**
 * @brief GET /api/live - 存活检查（用于 K8s 存活探测）
 */
static esp_err_t api_live_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"alive\":true}", 15);
    return ESP_OK;
}

esp_err_t health_handler_register(httpd_handle_t server)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // 健康检查
    httpd_uri_t health_uri = {
        .uri = "/api/health",
        .method = HTTP_GET,
        .handler = api_health_handler,
        .user_ctx = NULL
    };

    if (httpd_register_uri_handler(server, &health_uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/health");
        return ESP_FAIL;
    }

    // 就绪检查
    httpd_uri_t ready_uri = {
        .uri = "/api/ready",
        .method = HTTP_GET,
        .handler = api_ready_handler,
        .user_ctx = NULL
    };

    if (httpd_register_uri_handler(server, &ready_uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/ready");
        return ESP_FAIL;
    }

    // 存活检查
    httpd_uri_t live_uri = {
        .uri = "/api/live",
        .method = HTTP_GET,
        .handler = api_live_handler,
        .user_ctx = NULL
    };

    if (httpd_register_uri_handler(server, &live_uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/live");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Health handlers registered");
    return ESP_OK;
}

const char* health_get_status_string(void)
{
    server_context_t *ctx = server_context_get();

    if (!ctx) {
        return "no_context";
    }

    switch (ctx->state) {
        case SERVER_STATE_UNINITIALIZED:
            return "uninitialized";
        case SERVER_STATE_INITIALIZED:
            return "initialized";
        case SERVER_STATE_RUNNING:
            return "running";
        case SERVER_STATE_GRACEFUL_SHUTDOWN:
            return "shutting_down";
        case SERVER_STATE_STOPPED:
            return "stopped";
        default:
            return "unknown";
    }
}
