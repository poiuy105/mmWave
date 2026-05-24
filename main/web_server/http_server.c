/**
 * @file http_server.c
 * @brief HTTP 服务器实现
 */

#include "http_server.h"
#include "websocket_server.h"
#include "embedded_web.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <math.h>
#include <cJSON.h>

static const char *TAG = "HTTP_SERVER";
static httpd_handle_t s_server = NULL;

// 雷达数据模拟（用于测试）
static uint32_t s_frame_count = 0;
static TimerHandle_t s_radar_timer = NULL;

/* MIME 类型映射 */
static const struct {
    const char *ext;
    const char *mime;
} mime_types[] = {
    {".html", "text/html"},
    {".css", "text/css"},
    {".js", "application/javascript"},
    {".json", "application/json"},
    {".png", "image/png"},
    {".jpg", "image/jpeg"},
    {".gif", "image/gif"},
    {".svg", "image/svg+xml"},
    {".ico", "image/x-icon"},
    {".woff", "font/woff"},
    {".woff2", "font/woff2"},
    {".ttf", "font/ttf"},
    {NULL, "application/octet-stream"}
};

/**
 * @brief 获取文件的 MIME 类型
 */
static const char* get_mime_type(const char *filename)
{
    const char *ext = strrchr(filename, '.');
    if (ext) {
        for (int i = 0; mime_types[i].ext; i++) {
            if (strcasecmp(ext, mime_types[i].ext) == 0) {
                return mime_types[i].mime;
            }
        }
    }
    return "application/octet-stream";
}

/**
 * @brief 静态文件处理 handler
 */
static esp_err_t static_file_handler(httpd_req_t *req)
{
    char filepath[600];
    const char *uri = req->uri;
    
    // 默认首页
    if (strcmp(uri, "/") == 0) {
        uri = "/index.html";
    }
    
    // 对于 index.html，直接返回内嵌的 HTML
    if (strcmp(uri, "/index.html") == 0) {
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, embedded_index_html, strlen(embedded_index_html));
        ESP_LOGI(TAG, "Served embedded index.html");
        return ESP_OK;
    }
    
    // 构建文件路径
    int len = snprintf(filepath, sizeof(filepath), "/storage/www%s", uri);
    if (len >= sizeof(filepath)) {
        ESP_LOGW(TAG, "URI too long");
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    
    // 检查文件是否存在
    struct stat st;
    if (stat(filepath, &st) != 0) {
        ESP_LOGW(TAG, "File not found: %s", filepath);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    
    // 打开文件
    FILE *file = fopen(filepath, "r");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    // 设置 Content-Type
    const char *mime = get_mime_type(filepath);
    httpd_resp_set_type(req, mime);
    
    // 发送文件内容
    char buffer[1024];
    size_t read_bytes;
    while ((read_bytes = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        esp_err_t err = httpd_resp_send_chunk(req, buffer, read_bytes);
        if (err != ESP_OK) {
            fclose(file);
            return err;
        }
    }
    
    fclose(file);
    httpd_resp_send_chunk(req, NULL, 0);  // 结束响应
    
    ESP_LOGD(TAG, "Served: %s (%s)", uri, mime);
    return ESP_OK;
}

/**
 * @brief API 状态查询 handler
 */
static esp_err_t api_status_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "server", "running");
    cJSON_AddStringToObject(root, "version", "1.0.0");
    cJSON_AddNumberToObject(root, "websocket_clients", websocket_get_client_count());
    
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

/**
 * @brief API 系统信息 handler
 */
static esp_err_t api_system_info_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "chip_model", "ESP32-C3");
    cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "min_free_heap", esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(root, "frame_count", s_frame_count);
    cJSON_AddNumberToObject(root, "uptime", xTaskGetTickCount() / configTICK_RATE_HZ);
    
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

/**
 * @brief API 雷达状态 handler
 */
static esp_err_t api_radar_status_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "running", true);
    cJSON_AddStringToObject(root, "type", "LD2460");
    cJSON_AddNumberToObject(root, "target_count", 0);
    cJSON_AddStringToObject(root, "mount_mode", "side");
    
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

/**
 * @brief API 配置获取 handler
 */
static esp_err_t api_config_get_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    
    // 雷达配置
    cJSON *radar = cJSON_CreateObject();
    cJSON_AddStringToObject(radar, "type", "LD2460");
    cJSON_AddNumberToObject(radar, "baud_rate", 115200);
    cJSON_AddStringToObject(radar, "mount_mode", "side");
    cJSON_AddNumberToObject(radar, "room_width", 6.0);
    cJSON_AddNumberToObject(radar, "room_depth", 8.0);
    cJSON_AddNumberToObject(radar, "room_height", 3.0);
    cJSON_AddItemToObject(root, "radar", radar);
    
    // 显示配置
    cJSON *display = cJSON_CreateObject();
    cJSON_AddNumberToObject(display, "grid_size", 0.5);
    cJSON_AddNumberToObject(display, "trail_length", 60);
    cJSON_AddBoolToObject(display, "show_grid", true);
    cJSON_AddBoolToObject(display, "show_trail", true);
    cJSON_AddItemToObject(root, "display", display);
    
    // 网络配置
    cJSON *network = cJSON_CreateObject();
    cJSON_AddStringToObject(network, "wifi_mode", "ap");
    cJSON_AddStringToObject(network, "ssid", "LD-Radar-AP");
    cJSON_AddItemToObject(root, "network", network);
    
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

/**
 * @brief API 配置更新 handler
 */
static esp_err_t api_config_put_handler(httpd_req_t *req)
{
    char buf[1024];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request body");
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    ESP_LOGI(TAG, "Config update: %s", buf);
    
    // TODO: 保存配置到 NVS
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddStringToObject(root, "message", "Configuration saved");
    
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

/**
 * @brief API 日志获取 handler
 */
static esp_err_t api_logs_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *logs = cJSON_CreateArray();
    
    // 模拟日志数据
    cJSON *log1 = cJSON_CreateObject();
    cJSON_AddStringToObject(log1, "time", "2024-01-01 12:00:00");
    cJSON_AddStringToObject(log1, "level", "INFO");
    cJSON_AddStringToObject(log1, "message", "System started");
    cJSON_AddItemToArray(logs, log1);
    
    cJSON_AddItemToObject(root, "logs", logs);
    cJSON_AddNumberToObject(root, "total", 1);
    
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

/**
 * @brief CORS 预检 handler
 */
static esp_err_t api_options_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/**
 * @brief WebSocket 连接回调
 */
static void ws_on_connect(int sockfd)
{
    ESP_LOGI(TAG, "WebSocket client connected: fd=%d", sockfd);
}

/**
 * @brief WebSocket 断开回调
 */
static void ws_on_disconnect(int sockfd)
{
    ESP_LOGI(TAG, "WebSocket client disconnected: fd=%d", sockfd);
}

/**
 * @brief WebSocket 消息回调
 */
static void ws_on_message(int sockfd, const uint8_t *data, size_t len, ws_frame_type_t type)
{
    ESP_LOGI(TAG, "WebSocket message from fd=%d: %s", sockfd, (char*)data);
    
    // 处理订阅请求
    cJSON *msg = cJSON_Parse((char*)data);
    if (msg) {
        cJSON *type = cJSON_GetObjectItem(msg, "type");
        if (type && cJSON_IsString(type)) {
            if (strcmp(type->valuestring, "subscribe") == 0) {
                // 发送确认
                websocket_send_text(sockfd, "{\"type\":\"subscribed\"}");
            } else if (strcmp(type->valuestring, "ping") == 0) {
                // 发送 pong
                websocket_send_text(sockfd, "{\"type\":\"pong\"}");
            }
        }
        cJSON_Delete(msg);
    }
}

/**
 * @brief 雷达数据广播定时器回调
 */
static void radar_broadcast_callback(TimerHandle_t xTimer)
{
    if (websocket_get_client_count() == 0) {
        return;
    }
    
    s_frame_count++;
    
    // 构建模拟雷达数据
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "radar_data");
    cJSON_AddNumberToObject(root, "timestamp", xTaskGetTickCount());
    cJSON_AddNumberToObject(root, "frame_id", s_frame_count);
    
    // 模拟目标数据
    cJSON *targets = cJSON_CreateArray();
    
    // 生成 1-3 个随机目标
    int num_targets = (s_frame_count % 3) + 1;
    for (int i = 0; i < num_targets; i++) {
        cJSON *target = cJSON_CreateObject();
        cJSON_AddNumberToObject(target, "id", i + 1);
        
        // 模拟运动轨迹（圆周运动）
        float angle = (s_frame_count * 0.1f) + (i * 2.0f);
        float radius = 2.0f + i * 0.5f;
        float x = radius * cosf(angle);
        float y = radius * sinf(angle);
        float z = 1.5f + 0.5f * sinf(angle * 0.5f);
        
        cJSON_AddNumberToObject(target, "x", x);
        cJSON_AddNumberToObject(target, "y", y);
        cJSON_AddNumberToObject(target, "z", z);
        cJSON_AddNumberToObject(target, "vx", -radius * sinf(angle) * 0.1f);
        cJSON_AddNumberToObject(target, "vy", radius * cosf(angle) * 0.1f);
        cJSON_AddNumberToObject(target, "vz", 0.0f);
        cJSON_AddNumberToObject(target, "snr", 30.0f + (s_frame_count % 10));
        
        cJSON_AddItemToArray(targets, target);
    }
    
    cJSON_AddItemToObject(root, "targets", targets);
    cJSON_AddNumberToObject(root, "target_count", num_targets);
    
    char *json = cJSON_PrintUnformatted(root);
    websocket_broadcast_text(json);
    
    free(json);
    cJSON_Delete(root);
}

/**
 * @brief URI 处理配置
 */
static const httpd_uri_t uri_handlers[] = {
    // 静态文件
    {
        .uri = "/",
        .method = HTTP_GET,
        .handler = static_file_handler,
        .user_ctx = NULL
    },
    {
        .uri = "/index.html",
        .method = HTTP_GET,
        .handler = static_file_handler,
        .user_ctx = NULL
    },
    {
        .uri = "/css/*",
        .method = HTTP_GET,
        .handler = static_file_handler,
        .user_ctx = NULL
    },
    {
        .uri = "/js/*",
        .method = HTTP_GET,
        .handler = static_file_handler,
        .user_ctx = NULL
    },
    {
        .uri = "/assets/*",
        .method = HTTP_GET,
        .handler = static_file_handler,
        .user_ctx = NULL
    },
    // API 端点
    {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = api_status_handler,
        .user_ctx = NULL
    },
    {
        .uri = "/api/system/info",
        .method = HTTP_GET,
        .handler = api_system_info_handler,
        .user_ctx = NULL
    },
    {
        .uri = "/api/radar/status",
        .method = HTTP_GET,
        .handler = api_radar_status_handler,
        .user_ctx = NULL
    },
    {
        .uri = "/api/config",
        .method = HTTP_GET,
        .handler = api_config_get_handler,
        .user_ctx = NULL
    },
    {
        .uri = "/api/config",
        .method = HTTP_PUT,
        .handler = api_config_put_handler,
        .user_ctx = NULL
    },
    {
        .uri = "/api/logs",
        .method = HTTP_GET,
        .handler = api_logs_handler,
        .user_ctx = NULL
    },
    {
        .uri = "/api/*",
        .method = HTTP_OPTIONS,
        .handler = api_options_handler,
        .user_ctx = NULL
    }
};

esp_err_t http_server_start(void)
{
    if (s_server != NULL) {
        ESP_LOGW(TAG, "Server already running");
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 8192;
    config.task_priority = 5;
    config.max_uri_handlers = 16;
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server: %s", esp_err_to_name(err));
        return err;
    }

    // 注册 URI handlers
    for (int i = 0; i < sizeof(uri_handlers) / sizeof(uri_handlers[0]); i++) {
        err = httpd_register_uri_handler(s_server, &uri_handlers[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register handler %d: %s", i, esp_err_to_name(err));
            httpd_stop(s_server);
            s_server = NULL;
            return err;
        }
    }

    // 初始化 WebSocket 服务器
    ws_config_t ws_conf = {
        .on_connect = ws_on_connect,
        .on_disconnect = ws_on_disconnect,
        .on_message = ws_on_message,
        .task_stack_size = 4096,
        .task_priority = 5
    };

    err = websocket_init(s_server, &ws_conf);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WebSocket init failed: %s", esp_err_to_name(err));
        // 继续运行，不中断 HTTP 服务
    }

    // 创建雷达数据广播定时器 (100ms = 10Hz)
    s_radar_timer = xTimerCreate("radar_timer", pdMS_TO_TICKS(100),
                                  pdTRUE, NULL, radar_broadcast_callback);
    if (s_radar_timer) {
        xTimerStart(s_radar_timer, 0);
        ESP_LOGI(TAG, "Radar broadcast timer started (10Hz)");
    }

    ESP_LOGI(TAG, "HTTP server started successfully");
    return ESP_OK;
}

esp_err_t http_server_stop(void)
{
    if (s_server == NULL) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping HTTP server");

    // 停止定时器
    if (s_radar_timer) {
        xTimerStop(s_radar_timer, 0);
        xTimerDelete(s_radar_timer, 0);
        s_radar_timer = NULL;
    }

    esp_err_t err = httpd_stop(s_server);
    if (err == ESP_OK) {
        s_server = NULL;
    }
    return err;
}

bool http_server_is_running(void)
{
    return s_server != NULL;
}
