/**
 * @file http_server.c
 * @brief HTTP 服务器实现
 */

#include "http_server.h"
#include "websocket_server.h"
#include "file_manager.h"
#include "embedded_web.h"
#include "upload_page.h"
#include "radar_adapter/radar_adapter.h"
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

// 雷达数据广播定时器
static TimerHandle_t s_radar_timer = NULL;

/* MIME 类型映射 */
static const struct {
    const char *ext;
    const char *mime;
} mime_types[] = {
    {".html", "text/html"},
    {".htm", "text/html"},
    {".css", "text/css"},
    {".js", "application/javascript"},
    {".json", "application/json"},
    {".png", "image/png"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".gif", "image/gif"},
    {".svg", "image/svg+xml"},
    {".ico", "image/x-icon"},
    {".woff", "font/woff"},
    {".woff2", "font/woff2"},
    {".ttf", "font/ttf"},
    {".map", "application/json"},
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
    
    // 根路径 -> 文件管理页面
    if (strcmp(uri, "/") == 0) {
        // 检查 FATFS 中是否有 index.html
        if (file_manager_exists("/storage/www/index.html")) {
            uri = "/index.html";
            // 继续走 FATFS 逻辑
        } else {
            // 返回内嵌的文件管理页面
            httpd_resp_set_type(req, "text/html");
            httpd_resp_send(req, upload_page_html, strlen(upload_page_html));
            ESP_LOGI(TAG, "Served embedded upload page");
            return ESP_OK;
        }
    }
    
    // /app/ 路径 -> 雷达监控应用
    if (strncmp(uri, "/app", 4) == 0) {
        if (strcmp(uri, "/app") == 0 || strcmp(uri, "/app/") == 0) {
            uri = "/index.html";
        } else {
            uri = uri + 4;  // 去掉 /app 前缀
            if (uri[0] == '\0') uri = "/index.html";
        }
    }
    
    // 默认首页
    if (strcmp(uri, "/") == 0) {
        uri = "/index.html";
    }
    
    // 构建文件路径
    snprintf(filepath, sizeof(filepath), "/storage/www%s", uri);
    
    // 检查文件是否存在
    struct stat st;
    if (stat(filepath, &st) == 0 && S_ISREG(st.st_mode)) {
        // 从 FATFS 读取文件
        FILE *file = fopen(filepath, "r");
        if (!file) {
            ESP_LOGE(TAG, "Failed to open file: %s", filepath);
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        
        const char *mime = get_mime_type(filepath);
        httpd_resp_set_type(req, mime);
        
        char buffer[2048];
        size_t read_bytes;
        while ((read_bytes = fread(buffer, 1, sizeof(buffer), file)) > 0) {
            if (httpd_resp_send_chunk(req, buffer, read_bytes) != ESP_OK) {
                fclose(file);
                return ESP_FAIL;
            }
        }
        
        fclose(file);
        httpd_resp_send_chunk(req, NULL, 0);
        ESP_LOGD(TAG, "Served from FATFS: %s", uri);
        return ESP_OK;
    }
    
    // FATFS 中没有，尝试内嵌资源
    if (strcmp(uri, "/index.html") == 0) {
        // 返回内嵌的雷达监控页面
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, embedded_index_html, strlen(embedded_index_html));
        ESP_LOGI(TAG, "Served embedded radar app");
        return ESP_OK;
    }
    
    // 文件不存在
    ESP_LOGW(TAG, "File not found: %s", uri);
    httpd_resp_send_404(req);
    return ESP_FAIL;
}

/**
 * @brief API 状态查询 handler
 */
static esp_err_t api_status_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "server", "running");
    cJSON_AddStringToObject(root, "version", "1.1.0");
    cJSON_AddNumberToObject(root, "websocket_clients", websocket_get_client_count());
    cJSON_AddBoolToObject(root, "fatfs_ready", file_manager_is_ready());
    
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
    radar_status_t radar_status;
    radar_adapter_get_status(&radar_status);
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "chip_model", "ESP32-C3");
    cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "min_free_heap", esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(root, "radar_frames", radar_status.frame_count);
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
    radar_info_t info;
    radar_status_t status;
    radar_frame_t frame;
    
    radar_adapter_get_info(&info);
    radar_adapter_get_status(&status);
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "initialized", status.initialized);
    cJSON_AddBoolToObject(root, "report_enabled", status.report_enabled);
    cJSON_AddStringToObject(root, "type", info.type);
    cJSON_AddStringToObject(root, "name", info.name);
    cJSON_AddNumberToObject(root, "max_targets", info.max_targets);
    cJSON_AddNumberToObject(root, "frame_count", status.frame_count);
    cJSON_AddNumberToObject(root, "error_count", status.error_count);
    
    // 当前目标数
    if (radar_adapter_get_frame(&frame) == ESP_OK) {
        cJSON_AddNumberToObject(root, "target_count", frame.target_count);
    } else {
        cJSON_AddNumberToObject(root, "target_count", 0);
    }
    
    // 能力
    uint32_t caps = radar_adapter_get_capabilities();
    cJSON *capabilities = cJSON_CreateObject();
    cJSON_AddBoolToObject(capabilities, "has_3d", (caps & RADAR_CAP_3D) != 0);
    cJSON_AddBoolToObject(capabilities, "has_install_mode", (caps & RADAR_CAP_INSTALL_MODE) != 0);
    cJSON_AddBoolToObject(capabilities, "has_region_filter", (caps & RADAR_CAP_REGION_FILTER) != 0);
    cJSON_AddBoolToObject(capabilities, "has_sensitivity", (caps & RADAR_CAP_SENSITIVITY) != 0);
    cJSON_AddItemToObject(root, "capabilities", capabilities);
    
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
    cJSON_AddStringToObject(network, "wifi_mode", "sta");
    cJSON_AddStringToObject(network, "ssid", "FUCK_cao");
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
 * @brief API 文件列表 handler
 */
static esp_err_t api_files_list_handler(httpd_req_t *req)
{
    char path[256] = "/storage/www";
    
    // 从查询参数获取路径
    char query[256];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char path_param[256];
        if (httpd_query_key_value(query, "path", path_param, sizeof(path_param)) == ESP_OK) {
            strncpy(path, path_param, sizeof(path) - 1);
        }
    }
    
    file_list_t list;
    esp_err_t err = file_manager_list(path, &list);
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "path", path);
    
    if (err == ESP_OK) {
        cJSON *files = cJSON_CreateArray();
        for (int i = 0; i < list.count; i++) {
            cJSON *file = cJSON_CreateObject();
            cJSON_AddStringToObject(file, "name", list.files[i].name);
            cJSON_AddStringToObject(file, "path", list.files[i].path);
            cJSON_AddNumberToObject(file, "size", list.files[i].size);
            cJSON_AddBoolToObject(file, "is_dir", list.files[i].is_dir);
            cJSON_AddItemToArray(files, file);
        }
        cJSON_AddItemToObject(root, "files", files);
        cJSON_AddNumberToObject(root, "count", list.count);
        file_manager_list_free(&list);
    } else {
        cJSON_AddItemToObject(root, "files", cJSON_CreateArray());
        cJSON_AddNumberToObject(root, "count", 0);
    }
    
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

/**
 * @brief API 文件上传 handler
 */
static esp_err_t api_files_upload_handler(httpd_req_t *req)
{
    // 获取目标路径
    char path[512] = {0};
    char query[512];
    
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "path", path, sizeof(path));
    }
    
    if (strlen(path) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing path parameter");
        return ESP_FAIL;
    }
    
    // 接收文件内容
    int content_len = req->content_len;
    uint8_t *buffer = NULL;
    
    if (content_len > 0) {
        buffer = (uint8_t *)malloc(content_len);
        if (!buffer) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
            return ESP_FAIL;
        }
        
        int received = httpd_req_recv(req, (char *)buffer, content_len);
        if (received != content_len) {
            free(buffer);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to receive data");
            return ESP_FAIL;
        }
    }
    
    // 保存文件
    esp_err_t err = file_manager_upload(path, buffer, content_len);
    
    if (buffer) free(buffer);
    
    cJSON *root = cJSON_CreateObject();
    if (err == ESP_OK) {
        cJSON_AddBoolToObject(root, "success", true);
        cJSON_AddStringToObject(root, "path", path);
        cJSON_AddNumberToObject(root, "size", content_len);
    } else {
        cJSON_AddBoolToObject(root, "success", false);
        cJSON_AddStringToObject(root, "message", "Upload failed");
    }
    
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

/**
 * @brief API 文件删除 handler
 */
static esp_err_t api_files_delete_handler(httpd_req_t *req)
{
    char path[512] = {0};
    char query[512];
    
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "path", path, sizeof(path));
    }
    
    if (strlen(path) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing path parameter");
        return ESP_FAIL;
    }
    
    esp_err_t err = file_manager_delete(path);
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", err == ESP_OK);
    if (err != ESP_OK) {
        cJSON_AddStringToObject(root, "message", "Delete failed");
    }
    
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

/**
 * @brief API 文件系统信息 handler
 */
static esp_err_t api_fs_info_handler(httpd_req_t *req)
{
    fs_info_t info;
    esp_err_t err = file_manager_get_fs_info(&info);
    
    cJSON *root = cJSON_CreateObject();
    if (err == ESP_OK) {
        cJSON_AddNumberToObject(root, "total_bytes", info.total_bytes);
        cJSON_AddNumberToObject(root, "used_bytes", info.used_bytes);
        cJSON_AddNumberToObject(root, "free_bytes", info.free_bytes);
        cJSON_AddNumberToObject(root, "file_count", info.file_count);
        cJSON_AddNumberToObject(root, "dir_count", info.dir_count);
    }
    cJSON_AddBoolToObject(root, "ready", file_manager_is_ready());
    
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

/**
 * @brief API 格式化存储 handler
 */
static esp_err_t api_fs_format_handler(httpd_req_t *req)
{
    esp_err_t err = file_manager_format();
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", err == ESP_OK);
    if (err != ESP_OK) {
        cJSON_AddStringToObject(root, "message", "Format failed");
    }
    
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

/**
 * @brief API 雷达安装模式获取 handler (LD2460 特有)
 */
static esp_err_t api_radar_install_mode_get_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    
#if defined(CONFIG_RADAR_LD2460)
    ld2460_install_mode_t mode;
    ld2460_install_params_t params;
    
    radar_handle_t radar = radar_adapter_get_handle();
    if (radar && ld2460_get_install_mode(radar, &mode) == ESP_OK) {
        cJSON_AddStringToObject(root, "mode", mode == LD2460_INSTALL_TOP ? "top" : "side");
    }
    if (radar && ld2460_get_install_params(radar, &params) == ESP_OK) {
        cJSON_AddNumberToObject(root, "height", params.height);
        cJSON_AddNumberToObject(root, "angle", params.angle);
    }
    cJSON_AddBoolToObject(root, "supported", true);
#else
    cJSON_AddBoolToObject(root, "supported", false);
    cJSON_AddStringToObject(root, "message", "Not supported by this radar");
#endif
    
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

/**
 * @brief API 雷达安装模式设置 handler (LD2460 特有)
 */
static esp_err_t api_radar_install_mode_set_handler(httpd_req_t *req)
{
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request body");
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    cJSON *root = cJSON_CreateObject();
    esp_err_t err = ESP_ERR_NOT_SUPPORTED;
    
#if defined(CONFIG_RADAR_LD2460)
    cJSON *json = cJSON_Parse(buf);
    if (json) {
        radar_handle_t radar = radar_adapter_get_handle();
        if (radar) {
            cJSON *mode_item = cJSON_GetObjectItem(json, "mode");
            cJSON *height_item = cJSON_GetObjectItem(json, "height");
            cJSON *angle_item = cJSON_GetObjectItem(json, "angle");
            
            if (mode_item && cJSON_IsString(mode_item)) {
                ld2460_install_mode_t mode = (strcmp(mode_item->valuestring, "top") == 0) 
                    ? LD2460_INSTALL_TOP : LD2460_INSTALL_SIDE;
                err = ld2460_set_install_mode(radar, mode);
            }
            
            if (height_item && angle_item && cJSON_IsNumber(height_item) && cJSON_IsNumber(angle_item)) {
                err = ld2460_set_install_params(radar, height_item->valuedouble, angle_item->valuedouble);
            }
        }
        cJSON_Delete(json);
    }
#endif
    
    cJSON_AddBoolToObject(root, "success", err == ESP_OK);
    if (err != ESP_OK) {
        cJSON_AddStringToObject(root, "message", err == ESP_ERR_NOT_SUPPORTED ? 
            "Not supported by this radar" : "Operation failed");
    }
    
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

/**
 * @brief API 雷达检测范围获取 handler
 */
static esp_err_t api_radar_range_get_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    
#if defined(CONFIG_RADAR_LD2460)
    ld2460_range_t range;
    radar_handle_t radar = radar_adapter_get_handle();
    if (radar && ld2460_get_detection_range(radar, &range) == ESP_OK) {
        cJSON_AddNumberToObject(root, "distance", range.distance);
        cJSON_AddNumberToObject(root, "angle_start", range.angle_start);
        cJSON_AddNumberToObject(root, "angle_end", range.angle_end);
    }
    cJSON_AddBoolToObject(root, "supported", true);
#else
    cJSON_AddBoolToObject(root, "supported", false);
    cJSON_AddStringToObject(root, "message", "Not supported by this radar");
#endif
    
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

/**
 * @brief API 雷达检测范围设置 handler
 */
static esp_err_t api_radar_range_set_handler(httpd_req_t *req)
{
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request body");
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    cJSON *root = cJSON_CreateObject();
    esp_err_t err = ESP_ERR_NOT_SUPPORTED;
    
#if defined(CONFIG_RADAR_LD2460)
    cJSON *json = cJSON_Parse(buf);
    if (json) {
        radar_handle_t radar = radar_adapter_get_handle();
        if (radar) {
            cJSON *dist = cJSON_GetObjectItem(json, "distance");
            cJSON *angle_start = cJSON_GetObjectItem(json, "angle_start");
            cJSON *angle_end = cJSON_GetObjectItem(json, "angle_end");
            
            if (dist && angle_start && angle_end && 
                cJSON_IsNumber(dist) && cJSON_IsNumber(angle_start) && cJSON_IsNumber(angle_end)) {
                err = ld2460_set_detection_range(radar, dist->valuedouble, 
                    angle_start->valuedouble, angle_end->valuedouble);
            }
        }
        cJSON_Delete(json);
    }
#endif
    
    cJSON_AddBoolToObject(root, "success", err == ESP_OK);
    if (err != ESP_OK) {
        cJSON_AddStringToObject(root, "message", err == ESP_ERR_NOT_SUPPORTED ? 
            "Not supported by this radar" : "Operation failed");
    }
    
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
    ESP_LOGD(TAG, "WebSocket message from fd=%d: %s", sockfd, (char*)data);
    
    // 处理订阅请求
    cJSON *msg = cJSON_Parse((char*)data);
    if (msg) {
        cJSON *msg_type = cJSON_GetObjectItem(msg, "type");
        if (msg_type && cJSON_IsString(msg_type)) {
            if (strcmp(msg_type->valuestring, "subscribe") == 0) {
                websocket_send_text(sockfd, "{\"type\":\"subscribed\"}");
            } else if (strcmp(msg_type->valuestring, "ping") == 0) {
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
    
    // 获取真实雷达数据
    radar_frame_t frame;
    if (radar_adapter_get_frame(&frame) != ESP_OK) {
        return;  // 无数据则跳过
    }
    
    // 构建 JSON
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "radar_data");
    cJSON_AddNumberToObject(root, "timestamp", frame.timestamp_ms);
    cJSON_AddNumberToObject(root, "frame_id", frame.frame_id);
    
    // 目标数据
    cJSON *targets = cJSON_CreateArray();
    for (int i = 0; i < frame.target_count; i++) {
        cJSON *target = cJSON_CreateObject();
        cJSON_AddNumberToObject(target, "id", frame.targets[i].id);
        cJSON_AddNumberToObject(target, "x", frame.targets[i].x);
        cJSON_AddNumberToObject(target, "y", frame.targets[i].y);
        cJSON_AddNumberToObject(target, "z", frame.targets[i].z);
        cJSON_AddNumberToObject(target, "speed", frame.targets[i].speed);
        cJSON_AddNumberToObject(target, "snr", frame.targets[i].snr);
        cJSON_AddNumberToObject(target, "confidence", frame.targets[i].confidence);
        cJSON_AddItemToArray(targets, target);
    }
    
    cJSON_AddItemToObject(root, "targets", targets);
    cJSON_AddNumberToObject(root, "target_count", frame.target_count);
    
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
        .uri = "/app",
        .method = HTTP_GET,
        .handler = static_file_handler,
        .user_ctx = NULL
    },
    {
        .uri = "/app/*",
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
    {
        .uri = "/view/*",
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
    // 雷达特有功能 API
    {
        .uri = "/api/radar/install_mode",
        .method = HTTP_GET,
        .handler = api_radar_install_mode_get_handler,
        .user_ctx = NULL
    },
    {
        .uri = "/api/radar/install_mode",
        .method = HTTP_PUT,
        .handler = api_radar_install_mode_set_handler,
        .user_ctx = NULL
    },
    {
        .uri = "/api/radar/range",
        .method = HTTP_GET,
        .handler = api_radar_range_get_handler,
        .user_ctx = NULL
    },
    {
        .uri = "/api/radar/range",
        .method = HTTP_PUT,
        .handler = api_radar_range_set_handler,
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
    // 文件管理 API
    {
        .uri = "/api/files/list",
        .method = HTTP_GET,
        .handler = api_files_list_handler,
        .user_ctx = NULL
    },
    {
        .uri = "/api/files/upload",
        .method = HTTP_POST,
        .handler = api_files_upload_handler,
        .user_ctx = NULL
    },
    {
        .uri = "/api/files/delete",
        .method = HTTP_DELETE,
        .handler = api_files_delete_handler,
        .user_ctx = NULL
    },
    // 文件系统 API
    {
        .uri = "/api/fs/info",
        .method = HTTP_GET,
        .handler = api_fs_info_handler,
        .user_ctx = NULL
    },
    {
        .uri = "/api/fs/format",
        .method = HTTP_POST,
        .handler = api_fs_format_handler,
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

    // 初始化文件管理器
    esp_err_t err = file_manager_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "File manager init failed: %s", esp_err_to_name(err));
        // 继续运行，HTTP 服务仍然可用
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 8192;
    config.task_priority = 5;
    config.max_uri_handlers = 24;
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);

    err = httpd_start(&s_server, &config);
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
