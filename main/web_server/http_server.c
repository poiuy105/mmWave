/**
 * @file http_server.c
 * @brief HTTP/WebSocket server integration
 * Industrial-grade refactored version - modular architecture
 *
 * Usage:
 * 1. Call http_server_start() in app_main.c to start the server
 * 2. Call http_server_stop() in app_main.c to stop the server
 */

#include "http_server.h"
#include "http_server_core.h"
#include "server_context.h"
#include "server_config.h"
#include "ws_server.h"
#include "security/rate_limiter.h"
#include "security/input_validator.h"
#include "security/security_headers.h"
#include "handlers/health_handler.h"
#include "handlers/upload_handler.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include "radar_adapter/radar_adapter.h"
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <cJSON.h>

// HTTP status code for rate limiting (not always defined in esp_http_server.h)
#ifndef HTTPD_429_TOO_MANY_REQUESTS
#define HTTPD_429_TOO_MANY_REQUESTS 429
#endif
#ifndef HTTPD_503_SERVICE_UNAVAILABLE
#define HTTPD_503_SERVICE_UNAVAILABLE 503
#endif

static const char *TAG = "HTTP_SERVER";

// ============================================================
// Static file serving
// ============================================================

static const char* get_mime_type(const char *filename)
{
    const char *ext = strrchr(filename, '.');
    if (ext) {
        if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html";
        if (strcmp(ext, ".css") == 0) return "text/css";
        if (strcmp(ext, ".js") == 0) return "application/javascript";
        if (strcmp(ext, ".json") == 0) return "application/json";
        if (strcmp(ext, ".png") == 0) return "image/png";
        if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
        if (strcmp(ext, ".gif") == 0) return "image/gif";
        if (strcmp(ext, ".svg") == 0) return "image/svg+xml";
        if (strcmp(ext, ".ico") == 0) return "image/x-icon";
        if (strcmp(ext, ".woff") == 0) return "font/woff";
        if (strcmp(ext, ".woff2") == 0) return "font/woff2";
        if (strcmp(ext, ".ttf") == 0) return "font/ttf";
    }
    return "application/octet-stream";
}

// ============================================================
// WebSocket callbacks
// ============================================================

static void ws_on_connect(int fd, const char *client_ip)
{
    ESP_LOGI(TAG, "WebSocket client connected: fd=%d, ip=%s", fd, client_ip);
    server_stats_inc_ws_connection();
}

static void ws_on_disconnect(int fd)
{
    ESP_LOGI(TAG, "WebSocket client disconnected: fd=%d", fd);
    server_stats_inc_ws_disconnect();
}

static void ws_on_message(int fd, const uint8_t *data, size_t len, httpd_ws_type_t type)
{
    ESP_LOGD(TAG, "WebSocket message from fd=%d: type=%d, len=%lu",
             fd, type, (unsigned long)len);

    // Handle subscription requests
    if (type == HTTPD_WS_TYPE_TEXT) {
        cJSON *msg = cJSON_Parse((char*)data);
        if (msg) {
            cJSON *msg_type = cJSON_GetObjectItem(msg, "type");
            if (msg_type && cJSON_IsString(msg_type)) {
                if (strcmp(msg_type->valuestring, "subscribe") == 0) {
                    // Send subscription confirmation
                    ws_server_send_text(server_context_get()->ws_server, fd,
                                        "{\"type\":\"subscribed\"}");
                } else if (strcmp(msg_type->valuestring, "ping") == 0) {
                    ws_server_send_text(server_context_get()->ws_server, fd,
                                        "{\"type\":\"pong\"}");
                }
            }
            cJSON_Delete(msg);
        }
    }
}

// ============================================================
// HTTP Handlers
// ============================================================

static esp_err_t static_file_handler(httpd_req_t *req)
{
    server_context_t *ctx = server_context_get();

    // Check minimum heap before processing (prevent crashes under memory pressure)
    if (esp_get_free_heap_size() < 4096) {
        ESP_LOGW(TAG, "Low heap (%lu bytes), returning 503", (unsigned long)esp_get_free_heap_size());
        httpd_resp_send_err(req, HTTPD_503_SERVICE_UNAVAILABLE, "Server busy");
        return ESP_OK;  // Response already sent
    }

    char filepath[768];
    const char *uri = req->uri;

    // Rate limit check - use actual client IP
    if (ctx->config->rate_limit_enabled) {
        char client_ip[16] = "unknown";
        int fd = httpd_req_to_sockfd(req);
        if (fd >= 0) {
            struct sockaddr_in addr;
            socklen_t addr_len = sizeof(addr);
            if (getpeername(fd, (struct sockaddr *)&addr, &addr_len) == 0) {
                inet_ntop(AF_INET, &addr.sin_addr, client_ip, sizeof(client_ip));
            }
        }

        if (!rate_limiter_check(client_ip)) {
            server_stats_inc_rate_limit();
            httpd_resp_send_err(req, HTTPD_429_TOO_MANY_REQUESTS, "Rate limit exceeded");
            return ESP_OK;  // Response already sent
        }
    }

    // Security headers
    if (ctx->config->security_headers_enabled) {
        security_headers_set(req, NULL);
    }

    // Root path -> index.html
    if (strcmp(uri, "/") == 0) {
        uri = "/index.html";
    }

    // Build file path - use hardcoded defaults to avoid config copy issues
    snprintf(filepath, sizeof(filepath), "/storage/www%s", uri);

    ESP_LOGI(TAG, "Static file request: uri=%s, mount=%s, root=%s, filepath=%s",
             uri, ctx->config->fatfs_mount_path, ctx->config->static_file_root, filepath);

    // Check if file exists
    struct stat st;
    if (stat(filepath, &st) != 0 || !S_ISREG(st.st_mode)) {
        ESP_LOGW(TAG, "File not found: %s (heap=%lu, errno=%d)", filepath, (unsigned long)esp_get_free_heap_size(), errno);
        httpd_resp_send_404(req);
        return ESP_OK;  // Response already sent, return OK to prevent double-send
    }

    // Read and send file
    FILE *file = fopen(filepath, "r");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        httpd_resp_send_500(req);
        return ESP_OK;  // Response already sent
    }

    httpd_resp_set_type(req, get_mime_type(filepath));

    uint8_t buffer[2048];
    size_t read_bytes;
    esp_err_t ret = ESP_OK;

    while ((read_bytes = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (httpd_resp_send_chunk(req, (const char *)buffer, read_bytes) != ESP_OK) {
            ret = ESP_FAIL;
            break;
        }
    }

    fclose(file);
    httpd_resp_send_chunk(req, NULL, 0);

    server_stats_inc_request();
    return ret;
}

static esp_err_t api_status_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "server", "running");
    cJSON_AddStringToObject(root, "version", "2.0.0");

    server_context_t *ctx = server_context_get();
    if (ctx && ctx->ws_server) {
        cJSON_AddNumberToObject(root, "websocket_clients", ws_server_get_client_count(ctx->ws_server));
    }

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));

    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t api_system_info_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "chip_model", "ESP32-C3");
    cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "min_free_heap", esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(root, "uptime", xTaskGetTickCount() / configTICK_RATE_HZ);

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));

    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t api_options_handler(httpd_req_t *req)
{
    server_context_t *ctx = server_context_get();
    if (ctx->config->cors_enabled) {
        security_headers_set_cors(req, ctx->config->cors_origin);
    } else {
        security_headers_set_cors(req, "*");
    }
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// ============================================================
// URI Handler list
// ============================================================

static const httpd_uri_t uri_handlers[] = {
    // Static files (exact matches first)
    { .uri = "/",           .method = HTTP_GET, .handler = static_file_handler },
    { .uri = "/index.html", .method = HTTP_GET, .handler = static_file_handler },
    { .uri = "/upload",     .method = HTTP_GET, .handler = upload_page_handler },
    // Note: /* wildcard is registered LAST in http_server_start() to not override other handlers

    // API
    { .uri = "/api/status",      .method = HTTP_GET, .handler = api_status_handler },
    { .uri = "/api/system/info", .method = HTTP_GET, .handler = api_system_info_handler },
    { .uri = "/api/*",           .method = HTTP_OPTIONS, .handler = api_options_handler },

    // WebSocket /ws is registered by ws_server_create() - do NOT register here
};

// ============================================================
// Server start/stop
// ============================================================

esp_err_t http_server_start(void)
{
    ESP_LOGI(TAG, "Starting HTTP/WebSocket server...");

    esp_err_t ret;

    // 1. Load configuration
    server_config_t config;
    server_config_load(&config);

    // 2. Initialize server context
    ret = server_context_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init server context: %s", esp_err_to_name(ret));
        return ret;
    }

    // Verify config was copied correctly
    server_context_t *ctx_verify = server_context_get();
    ESP_LOGI(TAG, "Config verify: mount='%s', root='%s'", 
             ctx_verify->config->fatfs_mount_path, 
             ctx_verify->config->static_file_root);

    // 3. Initialize security modules (non-critical, graceful degradation)
    rate_limiter_config_t rl_config = {
        .max_requests = config.rate_limit_max_requests,
        .window_ms = config.rate_limit_window_ms,
        .block_duration_sec = config.rate_limit_block_duration,
        .max_entries = config.rate_limit_max_entries,
    };
    ret = rate_limiter_init(&rl_config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Rate limiter init failed (continuing without rate limiting)");
    }

    security_headers_init_default();

    // 4. Create HTTP server
    http_server_t *http = http_server_create(&config);
    if (http == NULL) {
        ESP_LOGE(TAG, "Failed to create HTTP server");
        return ESP_FAIL;
    }

    ret = http_server_core_start(http);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        http_server_core_destroy(http);
        return ret;
    }

    server_context_t *ctx = server_context_get();
    ctx->http_server_obj = (void *)http;
    ctx->http_server = http_server_core_get_handle(http);

    // 5. Register URI handlers (skip if already registered from previous crash)
    httpd_handle_t handle = http_server_core_get_handle(http);
    for (size_t i = 0; i < sizeof(uri_handlers) / sizeof(uri_handlers[0]); i++) {
        esp_err_t reg_ret = httpd_register_uri_handler(handle, &uri_handlers[i]);
        if (reg_ret == ESP_ERR_HTTPD_HANDLER_EXISTS) {
            ESP_LOGW(TAG, "Handler %s already registered, skipping", uri_handlers[i].uri);
        } else if (reg_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register handler %s: %s", uri_handlers[i].uri, esp_err_to_name(reg_ret));
            http_server_core_destroy(http);
            return reg_ret;
        }
    }

    // 6. Register health check handlers
    health_handler_register(handle);

    // 7. Register file upload/management handlers
    upload_handler_register(handle);

    // 8. Create WebSocket server
    if (config.ws_enabled) {
        ws_server_config_t ws_config = {
            .on_connect = ws_on_connect,
            .on_disconnect = ws_on_disconnect,
            .on_message = ws_on_message,
            .max_clients = config.ws_max_clients,
            .msg_queue_size = config.ws_msg_queue_size,
            .max_msg_size = config.ws_max_msg_size,
            .heartbeat_enabled = true,
            .heartbeat_interval = config.ws_heartbeat_interval,
            .heartbeat_timeout = config.ws_client_timeout,
        };

        ctx->ws_server = ws_server_create(handle, &ws_config);
        if (ctx->ws_server == NULL) {
            ESP_LOGW(TAG, "Failed to create WebSocket server, continuing without WS");
        }
    }

    // 9. Register /* wildcard LAST (after all exact matches)
    // This ensures /api/* and other handlers are matched before static file fallback
    httpd_uri_t wildcard_uri = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = static_file_handler,
        .user_ctx = NULL
    };
    esp_err_t wc_ret = httpd_register_uri_handler(handle, &wildcard_uri);
    if (wc_ret == ESP_ERR_HTTPD_HANDLER_EXISTS) {
        ESP_LOGW(TAG, "Handler /* already registered, skipping");
    } else if (wc_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /* wildcard: %s", esp_err_to_name(wc_ret));
    }

    ESP_LOGI(TAG, "HTTP/WebSocket server started successfully");
    ESP_LOGI(TAG, "  - HTTP:  http://<ip>:%d/", (int)config.http_port);
    ESP_LOGI(TAG, "  - WS:    ws://<ip>:%d/ws", (int)config.http_port);
    ESP_LOGI(TAG, "  - Health: http://<ip>:%d/api/health", (int)config.http_port);

    return ESP_OK;
}

esp_err_t http_server_stop(void)
{
    ESP_LOGI(TAG, "Stopping HTTP/WebSocket server...");

    server_context_t *ctx = server_context_get();
    if (ctx == NULL) {
        return ESP_OK;
    }

    // 1. Stop WebSocket server
    if (ctx->ws_server) {
        ws_server_destroy(ctx->ws_server);
        ctx->ws_server = NULL;
    }

    // 2. Unregister URI handlers before stopping HTTP server (Fix #25)
    httpd_handle_t handle = (httpd_handle_t)ctx->http_server;
    if (handle) {
        httpd_unregister_uri_handler(handle, "/upload", HTTP_GET);
        httpd_unregister_uri_handler(handle, "/api/files/upload", HTTP_POST);
        httpd_unregister_uri_handler(handle, "/api/files/list", HTTP_GET);
        httpd_unregister_uri_handler(handle, "/api/files/delete", HTTP_DELETE);
        httpd_unregister_uri_handler(handle, "/api/fs/info", HTTP_GET);
        httpd_unregister_uri_handler(handle, "/api/fs/format", HTTP_POST);
        httpd_unregister_uri_handler(handle, "/api/health", HTTP_GET);
        httpd_unregister_uri_handler(handle, "/api/ready", HTTP_GET);
        httpd_unregister_uri_handler(handle, "/api/live", HTTP_GET);
        httpd_unregister_uri_handler(handle, "/api/status", HTTP_GET);
        httpd_unregister_uri_handler(handle, "/api/system/info", HTTP_GET);
    }

    // 3. Stop HTTP server - use http_server_obj for proper type
    if (ctx->http_server_obj) {
        http_server_core_destroy((http_server_t *)ctx->http_server_obj);
        ctx->http_server_obj = NULL;
        ctx->http_server = NULL;
    }

    // 3. Cleanup security modules
    rate_limiter_deinit();

    // 4. Cleanup server context
    server_context_deinit();

    ESP_LOGI(TAG, "HTTP/WebSocket server stopped");

    return ESP_OK;
}

bool http_server_is_running(void)
{
    server_context_t *ctx = server_context_get();
    if (ctx == NULL || ctx->http_server_obj == NULL) {
        return false;
    }
    return http_server_core_is_running((http_server_t *)ctx->http_server_obj);
}
