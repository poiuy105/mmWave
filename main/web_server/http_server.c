/**
 * @file http_server.c
 * @brief HTTP/WebSocket 服务器整合入口
 *
 * 工业级重构版 - 使用模块化架构
 *
 * 使用方法：
 * 1. 在 app_main.c 中调用 http_server_start() 启动服务器
 * 2. 在 app_main.c 中调用 http_server_stop() 停止服务器
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
#include "esp_log.h"
#include "esp_http_server.h"
#include "radar_adapter/radar_adapter.h"
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <cJSON.h>

// 前向声明（WebSocket handler 在 ws_server.c 中定义）
extern esp_err_t ws_uri_handler(httpd_req_t *req);

static const char *TAG = "HTTP_SERVER";

// ============================================================
// 静态文件服务
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
// WebSocket 回调
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
    ESP_LOGD(TAG, "WebSocket message from fd=%d: type=%d, len=%zu", fd, type, len);

    // 处理订阅请求
    if (type == HTTPD_WS_TYPE_TEXT) {
        cJSON *msg = cJSON_Parse((char*)data);
        if (msg) {
            cJSON *msg_type = cJSON_GetObjectItem(msg, "type");
            if (msg_type && cJSON_IsString(msg_type)) {
                if (strcmp(msg_type->valuestring, "subscribe") == 0) {
                    // 发送订阅确认
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
    char filepath[512];
    const char *uri = req->uri;
    server_context_t *ctx = server_context_get();

    // 速率限制检查
    if (ctx->config->rate_limit_enabled) {
        char client_ip[32];
        // get_client_ip(req, client_ip, sizeof(client_ip));
        strcpy(client_ip, "unknown");
        if (!rate_limiter_check(client_ip)) {
            server_stats_inc_rate_limit();
            httpd_resp_send_err(req, HTTPD_429_TOO_MANY_REQUESTS, "Rate limit exceeded");
            return ESP_FAIL;
        }
    }

    // 安全头
    if (ctx->config->security_headers_enabled) {
        security_headers_set(req, NULL);
    }

    // 根路径 -> index.html
    if (strcmp(uri, "/") == 0) {
        uri = "/index.html";
    }

    // 构建文件路径
    snprintf(filepath, sizeof(filepath), "%s%s%s",
             ctx->config->fatfs_mount_path,
             ctx->config->static_file_root,
             uri);

    // 检查文件是否存在
    struct stat st;
    if (stat(filepath, &st) != 0 || !S_ISREG(st.st_mode)) {
        ESP_LOGD(TAG, "File not found: %s", filepath);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    // 读取并发送文件
    FILE *file = fopen(filepath, "r");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        httpd_resp_send_500(req);
        return ESP_FAIL;
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
// URI Handler 列表
// ============================================================

static const httpd_uri_t uri_handlers[] = {
    // 静态文件
    { .uri = "/",           .method = HTTP_GET, .handler = static_file_handler },
    { .uri = "/index.html", .method = HTTP_GET, .handler = static_file_handler },
    { .uri = "/*",          .method = HTTP_GET, .handler = static_file_handler },

    // API
    { .uri = "/api/status",      .method = HTTP_GET, .handler = api_status_handler },
    { .uri = "/api/system/info",  .method = HTTP_GET, .handler = api_system_info_handler },
    { .uri = "/api/*",            .method = HTTP_OPTIONS, .handler = api_options_handler },

    // WebSocket
    { .uri = "/ws", .method = HTTP_GET, .handler = ws_uri_handler, .is_websocket = true },
};

// ============================================================
// 服务器启动/停止
// ============================================================

esp_err_t http_server_start(void)
{
    ESP_LOGI(TAG, "Starting HTTP/WebSocket server...");

    // 1. 加载配置
    server_config_t config;
    server_config_load(&config);
    server_config_to_string(&config, ESP_LOG_COLOR_I "[CONFIG] " ESP_LOG_RESET_COLOR "\n%s\n", 1024);

    // 2. 初始化服务器上下文
    ESP_ERROR_CHECK(server_context_init(&config));

    // 3. 初始化安全模块
    rate_limiter_config_t rl_config = {
        .max_requests = config.rate_limit_max_requests,
        .window_ms = config.rate_limit_window_ms,
        .block_duration_sec = config.rate_limit_block_duration,
        .max_entries = config.rate_limit_max_entries,
    };
    ESP_ERROR_CHECK(rate_limiter_init(&rl_config));

    security_headers_init_default();

    // 4. 创建 HTTP 服务器
    http_server_t *http = http_server_create(&config);
    if (http == NULL) {
        ESP_LOGE(TAG, "Failed to create HTTP server");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(http_server_start(http));

    server_context_t *ctx = server_context_get();
    ctx->http_server = http_server_get_handle(http);

    // 5. 注册 URI handlers
    httpd_handle_t handle = http_server_get_handle(http);
    for (size_t i = 0; i < sizeof(uri_handlers) / sizeof(uri_handlers[0]); i++) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(handle, &uri_handlers[i]));
    }

    // 6. 注册健康检查 handlers
    health_handler_register(handle);

    // 7. 创建 WebSocket 服务器
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

    ESP_LOGI(TAG, "HTTP/WebSocket server started successfully");
    ESP_LOGI(TAG, "  - HTTP:  http://<ip>:%d/", config.http_port);
    ESP_LOGI(TAG, "  - WS:    ws://<ip>:%d/ws", config.http_port);
    ESP_LOGI(TAG, "  - Health: http://<ip>:%d/api/health", config.http_port);

    return ESP_OK;
}

esp_err_t http_server_stop(void)
{
    ESP_LOGI(TAG, "Stopping HTTP/WebSocket server...");

    server_context_t *ctx = server_context_get();
    if (ctx == NULL) {
        return ESP_OK;
    }

    // 1. 停止 WebSocket 服务器
    if (ctx->ws_server) {
        ws_server_destroy(ctx->ws_server);
        ctx->ws_server = NULL;
    }

    // 2. 停止 HTTP 服务器
    if (ctx->http_server) {
        // 获取 http_server_t 句柄
        http_server_destroy((http_server_t *)ctx->http_server);
        ctx->http_server = NULL;
    }

    // 3. 清理安全模块
    rate_limiter_deinit();

    // 4. 清理服务器上下文
    server_context_deinit();

    ESP_LOGI(TAG, "HTTP/WebSocket server stopped");

    return ESP_OK;
}

bool http_server_is_running(void)
{
    return server_is_running();
}
