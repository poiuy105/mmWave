/**
 * @file health_handler.c
 * @brief Health check API implementation
 */

#include "health_handler.h"
#include "server_context.h"
#include "server_config.h"
#include "ws_server.h"
#include "app_wdt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <cJSON.h>

// HTTP status codes for error responses
#ifndef HTTPD_503_SERVICE_UNAVAILABLE
#define HTTPD_503_SERVICE_UNAVAILABLE 503
#endif

// Forward declaration for unused handlers warning suppression
static esp_err_t api_live_handler(httpd_req_t *req) __attribute__((unused));
static esp_err_t api_ready_handler(httpd_req_t *req) __attribute__((unused));

/**
 * @brief GET /api/health - Health check
 */
static esp_err_t api_health_handler(httpd_req_t *req)
{
    server_context_t *ctx = server_context_get();

    // Get a thread-safe snapshot of server statistics
    server_stats_t stats;
    server_stats_get_copy(&stats);

    cJSON *root = cJSON_CreateObject();

    // Basic status
    if (ctx && ctx->state == SERVER_STATE_RUNNING) {
        cJSON_AddStringToObject(root, "status", "healthy");
    } else {
        cJSON_AddStringToObject(root, "status", "unhealthy");
    }

    cJSON_AddStringToObject(root, "version", "2.0.0");
    cJSON_AddNumberToObject(root, "uptime", stats.uptime_seconds);

    // Memory status
    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t min_free = esp_get_minimum_free_heap_size();
    cJSON_AddNumberToObject(root, "free_heap", free_heap);
    cJSON_AddNumberToObject(root, "min_free_heap", min_free);

    // Update min heap in context (use stats API to avoid direct access)
    server_stats_update_heap();

    // HTTP statistics (from thread-safe copy)
    cJSON *http = cJSON_CreateObject();
    cJSON_AddNumberToObject(http, "total_requests", stats.total_requests);
    cJSON_AddNumberToObject(http, "active_requests", stats.active_requests);
    cJSON_AddNumberToObject(http, "error_requests", stats.error_requests);
    if (stats.total_requests > 0) {
        float error_rate = (float)stats.error_requests / stats.total_requests;
        cJSON_AddNumberToObject(http, "error_rate", error_rate);
    } else {
        cJSON_AddNumberToObject(http, "error_rate", 0.0);
    }
    cJSON_AddItemToObject(root, "http", http);

    // WebSocket statistics (from thread-safe copy)
    cJSON *ws = cJSON_CreateObject();
    cJSON_AddNumberToObject(ws, "connected_clients", ctx ? ws_server_get_client_count(ctx->ws_server) : 0);
    cJSON_AddNumberToObject(ws, "total_connections", stats.ws_connections);
    cJSON_AddNumberToObject(ws, "total_disconnections", stats.ws_disconnections);
    cJSON_AddNumberToObject(ws, "messages_sent", stats.ws_messages_sent);
    cJSON_AddNumberToObject(ws, "messages_failed", stats.ws_messages_failed);
    cJSON_AddItemToObject(root, "websocket", ws);

    // Error statistics (from thread-safe copy)
    cJSON *errors = cJSON_CreateObject();
    cJSON_AddNumberToObject(errors, "rate_limit_hits", stats.rate_limit_hits);
    cJSON_AddNumberToObject(errors, "validation_failures", stats.validation_failures);
    cJSON_AddNumberToObject(errors, "timeout_errors", stats.timeout_errors);
    cJSON_AddItemToObject(root, "errors", errors);

    // Broadcast status
    cJSON *broadcast = cJSON_CreateObject();
    if (ctx && ctx->config) {
        cJSON_AddBoolToObject(broadcast, "enabled", ctx->config->broadcast_enabled);
        cJSON_AddNumberToObject(broadcast, "interval", ctx->config->broadcast_interval);
    } else {
        cJSON_AddBoolToObject(broadcast, "enabled", false);
        cJSON_AddNumberToObject(broadcast, "interval", 0);
    }
    cJSON_AddItemToObject(root, "broadcast", broadcast);

    // Watchdog status
    cJSON *watchdog = cJSON_CreateObject();
    cJSON_AddNumberToObject(watchdog, "timeout", (double)app_wdt_get_timeout_sec());

    cJSON *wdt_tasks = cJSON_CreateObject();
    cJSON_AddStringToObject(wdt_tasks, "app",
        app_wdt_is_healthy(WDT_TASK_APP) ? "healthy" : "stalled");
    cJSON_AddStringToObject(wdt_tasks, "ws_heartbeat",
        app_wdt_is_healthy(WDT_TASK_WS_HEARTBEAT) ? "healthy" : "stalled");
    cJSON_AddStringToObject(wdt_tasks, "radar_broadcast",
        app_wdt_is_healthy(WDT_TASK_RADAR_BROADCAST) ? "healthy" : "stalled");
    cJSON_AddStringToObject(wdt_tasks, "radar_parse",
        app_wdt_is_healthy(WDT_TASK_RADAR_PARSE) ? "healthy" : "stalled");
    cJSON_AddItemToObject(watchdog, "tasks", wdt_tasks);

    cJSON_AddItemToObject(root, "watchdog", watchdog);

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));

    free(json);
    cJSON_Delete(root);

    return ESP_OK;
}

/**
 * @brief GET /api/ready - Readiness check (for K8s/Docker health probes)
 */
static esp_err_t api_ready_handler(httpd_req_t *req)
{
    server_context_t *ctx = server_context_get();

    // Check if server is running
    if (!ctx || ctx->state != SERVER_STATE_RUNNING) {
        httpd_resp_send_err(req, HTTPD_503_SERVICE_UNAVAILABLE, "Server not ready");
        return ESP_FAIL;
    }

    // Get thread-safe stats copy
    server_stats_t stats;
    server_stats_get_copy(&stats);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ready", true);
    cJSON_AddNumberToObject(root, "uptime", stats.uptime_seconds);

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));

    free(json);
    cJSON_Delete(root);

    return ESP_OK;
}

/**
 * @brief GET /api/live - Liveness check (for K8s liveness probes)
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

    // Health check
    httpd_uri_t health_uri = {
        .uri = "/api/health",
        .method = HTTP_GET,
        .handler = api_health_handler,
        .user_ctx = NULL
    };

    esp_err_t ret = httpd_register_uri_handler(server, &health_uri);
    if (ret == ESP_ERR_HTTPD_HANDLER_EXISTS) {
        ESP_LOGW("HEALTH", "/api/health already registered, skipping");
    } else if (ret != ESP_OK) {
        ESP_LOGE("HEALTH", "Failed to register /api/health: %s", esp_err_to_name(ret));
        return ret;
    }

    // Readiness check
    httpd_uri_t ready_uri = {
        .uri = "/api/ready",
        .method = HTTP_GET,
        .handler = api_ready_handler,
        .user_ctx = NULL
    };

    ret = httpd_register_uri_handler(server, &ready_uri);
    if (ret == ESP_ERR_HTTPD_HANDLER_EXISTS) {
        ESP_LOGW("HEALTH", "/api/ready already registered, skipping");
    } else if (ret != ESP_OK) {
        ESP_LOGE("HEALTH", "Failed to register /api/ready: %s", esp_err_to_name(ret));
        return ret;
    }

    // Liveness check
    httpd_uri_t live_uri = {
        .uri = "/api/live",
        .method = HTTP_GET,
        .handler = api_live_handler,
        .user_ctx = NULL
    };

    ret = httpd_register_uri_handler(server, &live_uri);
    if (ret == ESP_ERR_HTTPD_HANDLER_EXISTS) {
        ESP_LOGW("HEALTH", "/api/live already registered, skipping");
    } else if (ret != ESP_OK) {
        ESP_LOGE("HEALTH", "Failed to register /api/live: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI("HEALTH", "Health handlers registered");
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
