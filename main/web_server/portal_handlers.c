#include "portal_handlers.h"
#include "config/nvs_config.h"
#include "wifi/wifi_manager.h"
#include "core/state_machine.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "portal_handlers";

// 内嵌 HTML 配置页面（由 CMake embed_txtfile 生成）
extern const char portal_html_start[] asm("_binary_portal_html_start");
extern const char portal_html_end[] asm("_binary_portal_html_end");

// ============ 辅助函数 ============

static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json, strlen(json));
}

static esp_err_t send_success(httpd_req_t *req)
{
    return send_json(req, "{\"success\":true}");
}

static esp_err_t send_error(httpd_req_t *req, const char *message)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"success\":false,\"message\":\"%s\"}", message);
    return send_json(req, buf);
}

// ============ 配置页面 ============

extern const char portal_html_start[] asm("_binary_portal_html_start");
extern const char portal_html_end[] asm("_binary_portal_html_end");

esp_err_t portal_root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    const size_t html_len = portal_html_end - portal_html_start;
    return httpd_resp_send(req, portal_html_start, html_len);
}

// ============ WiFi 扫描 ============

esp_err_t portal_scan_handler(httpd_req_t *req)
{
    wifi_scan_results_t results;
    esp_err_t err = wifi_manager_scan_wifi(&results);
    if (err != ESP_OK) {
        return send_error(req, "WiFi scan failed");
    }

    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < results.count; i++) {
        cJSON *ap = cJSON_CreateObject();
        cJSON_AddStringToObject(ap, "ssid", results.aps[i].ssid);
        cJSON_AddNumberToObject(ap, "rssi", results.aps[i].rssi);
        cJSON_AddNumberToObject(ap, "channel", results.aps[i].channel);
        
        const char *security = (results.aps[i].authmode != WIFI_AUTH_OPEN) ? "SECURED" : "OPEN";
        cJSON_AddStringToObject(ap, "security", security);
        
        cJSON_AddItemToArray(root, ap);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    esp_err_t ret = send_json(req, json);
    free(json);
    return ret;
}

// ============ 获取配置 ============

esp_err_t portal_get_config_handler(httpd_req_t *req)
{
    app_config_t config;
    nvs_load_all_config(&config);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "wifi_ssid", config.wifi_ssid);
    cJSON_AddStringToObject(root, "wifi_password", config.wifi_password);
    cJSON_AddStringToObject(root, "mqtt_uri", config.mqtt_uri);
    cJSON_AddNumberToObject(root, "mqtt_port", config.mqtt_port);
    cJSON_AddStringToObject(root, "mqtt_username", config.mqtt_username);
    cJSON_AddStringToObject(root, "mqtt_password", config.mqtt_password);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    esp_err_t ret = send_json(req, json);
    free(json);
    return ret;
}

// ============ 保存配置（核心 API） ============

esp_err_t portal_post_config_handler(httpd_req_t *req)
{
    // 读取请求 body
    char buf[512] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        return send_error(req, "Failed to read request body");
    }

    // 解析 JSON
    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        return send_error(req, "Invalid JSON");
    }

    // 提取字段
    cJSON *wifi_ssid = cJSON_GetObjectItem(root, "wifi_ssid");
    cJSON *wifi_pass = cJSON_GetObjectItem(root, "wifi_password");
    cJSON *mqtt_uri  = cJSON_GetObjectItem(root, "mqtt_uri");
    cJSON *mqtt_port = cJSON_GetObjectItem(root, "mqtt_port");
    cJSON *mqtt_user = cJSON_GetObjectItem(root, "mqtt_username");
    cJSON *mqtt_pass = cJSON_GetObjectItem(root, "mqtt_password");

    // 校验必填字段
    if (!cJSON_IsString(wifi_ssid) || strlen(wifi_ssid->valuestring) == 0) {
        cJSON_Delete(root);
        return send_error(req, "wifi_ssid is required");
    }
    if (!cJSON_IsString(mqtt_uri) || strlen(mqtt_uri->valuestring) == 0) {
        cJSON_Delete(root);
        return send_error(req, "mqtt_uri is required");
    }

    // 保存 WiFi 配置
    const char *ssid = wifi_ssid->valuestring;
    const char *pass = cJSON_IsString(wifi_pass) ? wifi_pass->valuestring : "";
    uint16_t port = cJSON_IsNumber(mqtt_port) ? (uint16_t)mqtt_port->valuedouble : 1883;
    const char *user = cJSON_IsString(mqtt_user) ? mqtt_user->valuestring : "";
    const char *mpass = cJSON_IsString(mqtt_pass) ? mqtt_pass->valuestring : "";

    esp_err_t err = nvs_save_wifi_config(ssid, pass);
    if (err != ESP_OK) {
        cJSON_Delete(root);
        return send_error(req, "Failed to save WiFi config");
    }

    err = nvs_save_mqtt_config(mqtt_uri->valuestring, port, user, mpass);
    if (err != ESP_OK) {
        cJSON_Delete(root);
        return send_error(req, "Failed to save MQTT config");
    }

    // 标记非首次启动
    nvs_set_first_boot(false);

    // 回读验证
    app_config_t verify;
    nvs_load_all_config(&verify);
    if (strcmp(verify.wifi_ssid, ssid) != 0) {
        cJSON_Delete(root);
        return send_error(req, "Config verification failed");
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Config saved: SSID=%s, MQTT=%s:%d", ssid, mqtt_uri->valuestring, port);

    // 触发状态机事件：配置已收到
    state_machine_trigger_event(EVENT_CONFIG_RECEIVED);

    return send_success(req);
}

// ============ 获取状态 ============

esp_err_t portal_get_status_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "state", 
        state_machine_get_state_name(state_machine_get_state()));

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    esp_err_t ret = send_json(req, json);
    free(json);
    return ret;
}

// ============ Captive Portal 检测 ============

esp_err_t portal_captive_detect_handler(httpd_req_t *req)
{
    // 各平台 Captive Portal 检测返回 302 重定向到 /
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_set_hdr(req, "Content-Length", "0");
    return httpd_resp_send(req, NULL, 0);
}

esp_err_t portal_captive_redirect_handler(httpd_req_t *req)
{
    // Catch-all: 所有未匹配的 GET 请求重定向到 /
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_set_hdr(req, "Content-Length", "0");
    return httpd_resp_send(req, NULL, 0);
}

// ============ 注册所有 handler ============

esp_err_t portal_handlers_register(httpd_handle_t server)
{
    if (server == NULL) return ESP_ERR_INVALID_ARG;

    // 配置页面
    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = portal_root_handler };
    httpd_register_uri_handler(server, &root);

    // API
    httpd_uri_t scan = { .uri = "/api/scan", .method = HTTP_GET, .handler = portal_scan_handler };
    httpd_register_uri_handler(server, &scan);

    httpd_uri_t get_cfg = { .uri = "/api/config", .method = HTTP_GET, .handler = portal_get_config_handler };
    httpd_register_uri_handler(server, &get_cfg);

    httpd_uri_t post_cfg = { .uri = "/api/config", .method = HTTP_POST, .handler = portal_post_config_handler };
    httpd_register_uri_handler(server, &post_cfg);

    httpd_uri_t status = { .uri = "/api/status", .method = HTTP_GET, .handler = portal_get_status_handler };
    httpd_register_uri_handler(server, &status);

    // Captive Portal 检测（各平台）
    const char *captive_paths[] = {
        "/generate_204", "/gen_204", "/hotspot-detect.html",
        "/connecttest.txt", "/redirect", "/fwlink"
    };
    for (int i = 0; i < 6; i++) {
        httpd_uri_t cp = { 
            .uri = captive_paths[i], 
            .method = HTTP_GET, 
            .handler = portal_captive_detect_handler 
        };
        httpd_register_uri_handler(server, &cp);
    }

    // Catch-all 重定向
    httpd_uri_t catch_all = { .uri = "/*", .method = HTTP_GET, .handler = portal_captive_redirect_handler };
    httpd_register_uri_handler(server, &catch_all);

    ESP_LOGI(TAG, "Portal handlers registered (13 routes)");
    return ESP_OK;
}
