/**
 * @file view_config.c
 * @brief 视图配置管理 - NVS 存储实现
 */

#include "view_config.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "view_config";
static const char *NVS_NAMESPACE = "view_config";

// 默认配置
static const view_config_t DEFAULT_CONFIG = {
    .rotation = 0.0f,
    .scale = 60.0f,
    .offset_x = 0.0f,
    .offset_y = 0.0f,
    .valid = false
};

// 当前配置（内存缓存）
static view_config_t s_current_config;
static bool s_initialized = false;

esp_err_t view_config_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    // 初始化为默认值
    memcpy(&s_current_config, &DEFAULT_CONFIG, sizeof(view_config_t));

    // 从 NVS 加载
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "No saved view config, using defaults");
        s_initialized = true;
        return ESP_OK;
    }

    // 加载各字段
    float rotation = 0.0f;
    float scale = 60.0f;
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    uint8_t valid = 0;

    nvs_get_float(handle, "rotation", &rotation);
    nvs_get_float(handle, "scale", &scale);
    nvs_get_float(handle, "offset_x", &offset_x);
    nvs_get_float(handle, "offset_y", &offset_y);
    nvs_get_u8(handle, "valid", &valid);

    nvs_close(handle);

    s_current_config.rotation = rotation;
    s_current_config.scale = scale;
    s_current_config.offset_x = offset_x;
    s_current_config.offset_y = offset_y;
    s_current_config.valid = (valid != 0);

    ESP_LOGI(TAG, "Loaded view config: rotation=%.1f, scale=%.1f, offset=(%.1f, %.1f), valid=%d",
             rotation, scale, offset_x, offset_y, valid);

    s_initialized = true;
    return ESP_OK;
}

esp_err_t view_config_get(view_config_t *config)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(config, &s_current_config, sizeof(view_config_t));
    return s_current_config.valid ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t view_config_save(const view_config_t *config)
{
    if (!s_initialized || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    // 保存各字段
    nvs_set_float(handle, "rotation", config->rotation);
    nvs_set_float(handle, "scale", config->scale);
    nvs_set_float(handle, "offset_x", config->offset_x);
    nvs_set_float(handle, "offset_y", config->offset_y);
    nvs_set_u8(handle, "valid", 1);

    nvs_commit(handle);
    nvs_close(handle);

    // 更新内存缓存
    memcpy(&s_current_config, config, sizeof(view_config_t));
    s_current_config.valid = true;

    ESP_LOGI(TAG, "Saved view config: rotation=%.1f, scale=%.1f, offset=(%.1f, %.1f)",
             config->rotation, config->scale, config->offset_x, config->offset_y);

    return ESP_OK;
}

esp_err_t view_config_reset(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
    }

    // 重置为默认值
    memcpy(&s_current_config, &DEFAULT_CONFIG, sizeof(view_config_t));

    ESP_LOGI(TAG, "View config reset to defaults");
    return ESP_OK;
}

bool view_config_has_saved(void)
{
    return s_initialized && s_current_config.valid;
}
