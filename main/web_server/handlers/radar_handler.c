/**
 * @file radar_handler.c
 * @brief 雷达配置 API Handler 实现
 */

#include "radar_handler.h"
#include "radar_adapter/radar_adapter.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_system.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "RADAR_HANDLER";

// 雷达配置存储（NVS 键名）
#define NVS_NAMESPACE "radar_cfg"
#define KEY_INSTALL_MODE "install_mode"
#define KEY_INSTALL_HEIGHT "install_height"
#define KEY_INSTALL_ANGLE "install_angle"
#define KEY_RANGE_DISTANCE "range_dist"
#define KEY_RANGE_ANGLE_START "range_ang_start"
#define KEY_RANGE_ANGLE_END "range_ang_end"

// 默认配置
static struct {
    char install_mode[16];      // "side" 或 "top"
    float install_height;       // 安装高度（米）
    float install_angle;        // 安装角度（度）
    float range_distance;       // 检测距离（米）
    float range_angle_start;    // 角度范围起始
    float range_angle_end;      // 角度范围结束
} s_radar_config = {
    .install_mode = "side",
    .install_height = 2.5f,
    .install_angle = 0.0f,
    .range_distance = 5.0f,
    .range_angle_start = -60.0f,
    .range_angle_end = 60.0f
};

static bool s_config_loaded = false;

/**
 * @brief 加载雷达配置（从 NVS 或默认值）
 */
static esp_err_t load_radar_config(void)
{
    if (s_config_loaded) {
        return ESP_OK;
    }

    // TODO: 从 NVS 加载配置
    // 目前使用默认值，后续可添加 NVS 持久化

    s_config_loaded = true;
    return ESP_OK;
}

/**
 * @brief 发送 JSON 响应
 */
static esp_err_t send_json_response(httpd_req_t *req, const char *json, int status_code)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, status_code == 200 ? "200 OK" : "400 Bad Request");
    return httpd_resp_sendstr(req, json);
}

/**
 * @brief 发送错误响应
 */
static esp_err_t send_error_response(httpd_req_t *req, const char *message)
{
    cJSON *error = cJSON_CreateObject();
    cJSON_AddBoolToObject(error, "success", false);
    cJSON_AddStringToObject(error, "message", message);
    char *json = cJSON_PrintUnformatted(error);
    cJSON_Delete(error);

    esp_err_t ret = send_json_response(req, json, 400);
    free(json);
    return ret;
}

/**
 * @brief 发送成功响应
 */
static esp_err_t send_success_response(httpd_req_t *req, const char *message)
{
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    cJSON_AddStringToObject(resp, "message", message);
    char *json = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);

    esp_err_t ret = send_json_response(req, json, 200);
    free(json);
    return ret;
}

/**
 * @brief 读取请求体
 */
static char* read_request_body(httpd_req_t *req)
{
    size_t content_len = req->content_len;
    if (content_len == 0 || content_len > 4096) {
        return NULL;
    }

    char *buf = malloc(content_len + 1);
    if (buf == NULL) {
        return NULL;
    }

    int ret = httpd_req_recv(req, buf, content_len);
    if (ret <= 0) {
        free(buf);
        return NULL;
    }

    buf[ret] = '\0';
    return buf;
}

// ============================================================
// API Handlers
// ============================================================

esp_err_t api_radar_status_handler(httpd_req_t *req)
{
    load_radar_config();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "connected", true);
    cJSON_AddStringToObject(root, "status", "running");
    cJSON_AddStringToObject(root, "install_mode", s_radar_config.install_mode);
    cJSON_AddNumberToObject(root, "target_count", 0);  // TODO: 从 radar_adapter 获取

    // 添加雷达型号和能力信息
    radar_info_t info;
    if (radar_adapter_get_info(&info) == ESP_OK) {
        cJSON_AddStringToObject(root, "type", info.type);
        cJSON_AddStringToObject(root, "name", info.name);

        cJSON *caps = cJSON_CreateObject();
        cJSON_AddBoolToObject(caps, "has_install_mode", info.has_install_mode);
        cJSON_AddBoolToObject(caps, "has_3d", info.has_3d);
        cJSON_AddBoolToObject(caps, "has_region_filter", info.has_region_filter);
        cJSON_AddBoolToObject(caps, "has_sensitivity", info.has_sensitivity);
        cJSON_AddBoolToObject(caps, "has_sleep_monitoring", info.has_sleep_monitoring);
        cJSON_AddStringToObject(caps, "install_mode", s_radar_config.install_mode);
        cJSON_AddItemToObject(root, "capabilities", caps);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    esp_err_t ret = send_json_response(req, json, 200);
    free(json);
    return ret;
}

esp_err_t api_radar_install_mode_get_handler(httpd_req_t *req)
{
    load_radar_config();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "mode", s_radar_config.install_mode);
    cJSON_AddNumberToObject(root, "height", s_radar_config.install_height);
    cJSON_AddNumberToObject(root, "angle", s_radar_config.install_angle);

    // 添加能力信息
    cJSON *caps = cJSON_CreateObject();
    cJSON_AddBoolToObject(caps, "has_install_mode", true);
    cJSON_AddStringToObject(caps, "install_mode", s_radar_config.install_mode);
    cJSON_AddItemToObject(root, "capabilities", caps);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    esp_err_t ret = send_json_response(req, json, 200);
    free(json);
    return ret;
}

esp_err_t api_radar_install_mode_put_handler(httpd_req_t *req)
{
    load_radar_config();

    char *body = read_request_body(req);
    if (body == NULL) {
        return send_error_response(req, "Invalid request body");
    }

    cJSON *root = cJSON_Parse(body);
    free(body);

    if (root == NULL) {
        return send_error_response(req, "Invalid JSON");
    }

    // 解析 mode
    cJSON *mode_json = cJSON_GetObjectItem(root, "mode");
    if (mode_json && cJSON_IsString(mode_json)) {
        const char *mode = mode_json->valuestring;
        if (strcmp(mode, "side") == 0 || strcmp(mode, "top") == 0) {
            strncpy(s_radar_config.install_mode, mode, sizeof(s_radar_config.install_mode) - 1);
            s_radar_config.install_mode[sizeof(s_radar_config.install_mode) - 1] = '\0';
        }
    }

    // 解析 height
    cJSON *height_json = cJSON_GetObjectItem(root, "height");
    if (height_json && cJSON_IsNumber(height_json)) {
        float height = height_json->valuedouble;
        if (height > 0 && height < 10) {
            s_radar_config.install_height = height;
        }
    }

    // 解析 angle
    cJSON *angle_json = cJSON_GetObjectItem(root, "angle");
    if (angle_json && cJSON_IsNumber(angle_json)) {
        float angle = angle_json->valuedouble;
        if (angle >= -90 && angle <= 90) {
            s_radar_config.install_angle = angle;
        }
    }

    cJSON_Delete(root);

    ESP_LOGI(TAG, "Install mode updated: mode=%s, height=%.2f, angle=%.2f",
             s_radar_config.install_mode,
             s_radar_config.install_height,
             s_radar_config.install_angle);

    return send_success_response(req, "Install mode updated");
}

esp_err_t api_radar_range_get_handler(httpd_req_t *req)
{
    load_radar_config();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "distance", s_radar_config.range_distance);
    cJSON_AddNumberToObject(root, "angle_start", s_radar_config.range_angle_start);
    cJSON_AddNumberToObject(root, "angle_end", s_radar_config.range_angle_end);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    esp_err_t ret = send_json_response(req, json, 200);
    free(json);
    return ret;
}

esp_err_t api_radar_range_put_handler(httpd_req_t *req)
{
    load_radar_config();

    char *body = read_request_body(req);
    if (body == NULL) {
        return send_error_response(req, "Invalid request body");
    }

    cJSON *root = cJSON_Parse(body);
    free(body);

    if (root == NULL) {
        return send_error_response(req, "Invalid JSON");
    }

    // 解析 distance
    cJSON *dist_json = cJSON_GetObjectItem(root, "distance");
    if (dist_json && cJSON_IsNumber(dist_json)) {
        float dist = dist_json->valuedouble;
        if (dist > 0 && dist <= 20) {
            s_radar_config.range_distance = dist;
        }
    }

    // 解析 angle_start
    cJSON *start_json = cJSON_GetObjectItem(root, "angle_start");
    if (start_json && cJSON_IsNumber(start_json)) {
        float start = start_json->valuedouble;
        if (start >= -90 && start <= 90) {
            s_radar_config.range_angle_start = start;
        }
    }

    // 解析 angle_end
    cJSON *end_json = cJSON_GetObjectItem(root, "angle_end");
    if (end_json && cJSON_IsNumber(end_json)) {
        float end = end_json->valuedouble;
        if (end >= -90 && end <= 90) {
            s_radar_config.range_angle_end = end;
        }
    }

    cJSON_Delete(root);

    ESP_LOGI(TAG, "Range updated: distance=%.2f, angle_start=%.2f, angle_end=%.2f",
             s_radar_config.range_distance,
             s_radar_config.range_angle_start,
             s_radar_config.range_angle_end);

    return send_success_response(req, "Range updated");
}
