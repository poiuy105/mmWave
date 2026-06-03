/**
 * @file zone_config.c
 * @brief 区域配置管理 - NVS 存储实现
 *
 * 功能:
 * - 从 NVS 加载/保存区域配置
 * - 区域 CRUD 操作
 * - 运行时状态管理 (triggered, target_count)
 */

#include "zone_config.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

// Forward declaration for MQTT discovery callback
extern esp_err_t app_mqtt_publish_zone_discovery(uint8_t zone_id);
extern esp_err_t app_mqtt_remove_zone_discovery(uint8_t zone_id);

static const char *TAG = "zone_config";

// 内部状态
static zone_config_list_t s_zone_list;
static bool s_initialized = false;

// NVS key 生成辅助函数
static void zone_key(char *buf, size_t buf_size, uint8_t id, const char *suffix)
{
    snprintf(buf, buf_size, "zone_%d_%s", id, suffix);
}

esp_err_t zone_config_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    memset(&s_zone_list, 0, sizeof(s_zone_list));

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE_ZONES, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No zone config in NVS, starting with empty list");
        s_initialized = true;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    // 读取区域数量
    uint8_t count = 0;
    err = nvs_get_u8(handle, NVS_KEY_ZONE_COUNT, &count);
    if (err == ESP_OK && count > 0) {
        if (count > ZONE_MAX_COUNT) {
            count = ZONE_MAX_COUNT;
        }
        s_zone_list.count = count;

        // 读取每个区域的配置
        for (uint8_t i = 0; i < count; i++) {
            zone_config_t *zone = &s_zone_list.zones[i];
            zone->id = i + 1;  // ID 从 1 开始

            char key[32];
            size_t len;

            // 读取名称
            len = sizeof(zone->name);
            zone_key(key, sizeof(key), zone->id, "name");
            nvs_get_str(handle, key, zone->name, &len);

            // 读取点数
            uint8_t point_count = 0;
            zone_key(key, sizeof(key), zone->id, "pcount");
            nvs_get_u8(handle, key, &point_count);
            zone->point_count = (point_count > ZONE_MAX_POINTS) ? ZONE_MAX_POINTS : point_count;

            // 读取点坐标
            for (uint8_t p = 0; p < zone->point_count; p++) {
                zone_key(key, sizeof(key), zone->id, "pts");
                char pts_key[40];
                snprintf(pts_key, sizeof(pts_key), "%s_%d", key, p);
                uint32_t packed;
                err = nvs_get_u32(handle, pts_key, &packed);
                if (err == ESP_OK) {
                    // 解包: 高16位是x, 低16位是y (定点数, 小数8位)
                    int16_t x_fixed = (int16_t)(packed >> 16);
                    int16_t y_fixed = (int16_t)(packed & 0xFFFF);
                    zone->points[p][0] = x_fixed / 256.0f;
                    zone->points[p][1] = y_fixed / 256.0f;
                }
            }

            // 读取颜色
            len = sizeof(zone->color);
            zone_key(key, sizeof(key), zone->id, "color");
            nvs_get_str(handle, key, zone->color, &len);

            // 读取启用状态
            uint8_t enabled = 0;
            zone_key(key, sizeof(key), zone->id, "en");
            nvs_get_u8(handle, key, &enabled);
            zone->enabled = (enabled != 0);

            // 运行时状态初始化为 0
            zone->triggered = false;
            zone->target_count = 0;

            ESP_LOGI(TAG, "Loaded zone %d: %s, points=%d, enabled=%d",
                     zone->id, zone->name, zone->point_count, zone->enabled);
        }
    }

    nvs_close(handle);
    s_initialized = true;
    ESP_LOGI(TAG, "Zone config initialized, count=%d", s_zone_list.count);
    return ESP_OK;
}

void zone_config_deinit(void)
{
    memset(&s_zone_list, 0, sizeof(s_zone_list));
    s_initialized = false;
    ESP_LOGI(TAG, "Zone config deinitialized");
}

esp_err_t zone_config_get_all(zone_config_list_t *list)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (list == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(list, &s_zone_list, sizeof(zone_config_list_t));
    return ESP_OK;
}

zone_config_t* zone_config_get(uint8_t id)
{
    if (!s_initialized || id == 0) {
        return NULL;
    }

    for (uint8_t i = 0; i < s_zone_list.count; i++) {
        if (s_zone_list.zones[i].id == id) {
            return &s_zone_list.zones[i];
        }
    }
    return NULL;
}

esp_err_t zone_config_add(const zone_config_t *zone)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (zone == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_zone_list.count >= ZONE_MAX_COUNT) {
        ESP_LOGE(TAG, "Zone count limit reached: %d", ZONE_MAX_COUNT);
        return ESP_ERR_NO_MEM;
    }

    // 自动分配 ID (1-based)
    uint8_t new_id = 1;
    for (uint8_t i = 0; i < s_zone_list.count; i++) {
        if (s_zone_list.zones[i].id >= new_id) {
            new_id = s_zone_list.zones[i].id + 1;
        }
    }

    zone_config_t *new_zone = &s_zone_list.zones[s_zone_list.count];
    memcpy(new_zone, zone, sizeof(zone_config_t));
    new_zone->id = new_id;

    // 确保运行时状态初始化为 0
    new_zone->triggered = false;
    new_zone->target_count = 0;

    s_zone_list.count++;

    // Publish MQTT discovery for new zone
    app_mqtt_publish_zone_discovery(new_id);

    ESP_LOGI(TAG, "Added zone %d: %s, total=%d", new_id, new_zone->name, s_zone_list.count);
    return ESP_OK;
}

esp_err_t zone_config_update(uint8_t id, const zone_config_t *zone)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (zone == NULL || id == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    zone_config_t *existing = zone_config_get(id);
    if (existing == NULL) {
        ESP_LOGE(TAG, "Zone %d not found", id);
        return ESP_ERR_NOT_FOUND;
    }

    // 保留运行时状态
    bool old_triggered = existing->triggered;
    uint8_t old_target_count = existing->target_count;

    // 更新配置数据
    memcpy(existing, zone, sizeof(zone_config_t));

    // 恢复 ID 和运行时状态
    existing->id = id;
    existing->triggered = old_triggered;
    existing->target_count = old_target_count;

    // Re-publish MQTT discovery (name may have changed)
    app_mqtt_publish_zone_discovery(id);

    ESP_LOGI(TAG, "Updated zone %d: %s", id, existing->name);
    return ESP_OK;
}

esp_err_t zone_config_delete(uint8_t id)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (id == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int8_t index = -1;
    for (uint8_t i = 0; i < s_zone_list.count; i++) {
        if (s_zone_list.zones[i].id == id) {
            index = i;
            break;
        }
    }

    if (index < 0) {
        ESP_LOGE(TAG, "Zone %d not found", id);
        return ESP_ERR_NOT_FOUND;
    }

    // 移动数组元素填补空缺
    if (index < s_zone_list.count - 1) {
        memmove(&s_zone_list.zones[index],
                &s_zone_list.zones[index + 1],
                (s_zone_list.count - index - 1) * sizeof(zone_config_t));
    }

    s_zone_list.count--;
    // 清空最后一个元素
    memset(&s_zone_list.zones[s_zone_list.count], 0, sizeof(zone_config_t));

    // Remove MQTT discovery for deleted zone
    app_mqtt_remove_zone_discovery(id);

    ESP_LOGI(TAG, "Deleted zone %d, remaining=%d", id, s_zone_list.count);
    return ESP_OK;
}

esp_err_t zone_config_save(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE_ZONES, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing: %s", esp_err_to_name(err));
        return err;
    }

    // 保存区域数量
    err = nvs_set_u8(handle, NVS_KEY_ZONE_COUNT, s_zone_list.count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save zone count: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    // 保存每个区域的配置
    for (uint8_t i = 0; i < s_zone_list.count; i++) {
        const zone_config_t *zone = &s_zone_list.zones[i];
        char key[32];

        // 保存名称
        zone_key(key, sizeof(key), zone->id, "name");
        nvs_set_str(handle, key, zone->name);

        // 保存点数
        zone_key(key, sizeof(key), zone->id, "pcount");
        nvs_set_u8(handle, key, zone->point_count);

        // 保存点坐标 (打包为 u32: 高16位x, 低16位y)
        for (uint8_t p = 0; p < zone->point_count; p++) {
            zone_key(key, sizeof(key), zone->id, "pts");
            char pts_key[40];
            snprintf(pts_key, sizeof(pts_key), "%s_%d", key, p);

            // 定点数转换 (8位小数)
            int16_t x_fixed = (int16_t)(zone->points[p][0] * 256.0f);
            int16_t y_fixed = (int16_t)(zone->points[p][1] * 256.0f);
            uint32_t packed = ((uint32_t)(uint16_t)x_fixed << 16) | (uint16_t)y_fixed;

            nvs_set_u32(handle, pts_key, packed);
        }

        // 保存颜色
        zone_key(key, sizeof(key), zone->id, "color");
        nvs_set_str(handle, key, zone->color);

        // 保存启用状态
        zone_key(key, sizeof(key), zone->id, "en");
        nvs_set_u8(handle, key, zone->enabled ? 1 : 0);
    }

    // 提交更改
    err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Zone config saved, count=%d", s_zone_list.count);
    } else {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
    }

    return err;
}

esp_err_t zone_config_set_triggered(uint8_t id, bool triggered)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    zone_config_t *zone = zone_config_get(id);
    if (zone == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    zone->triggered = triggered;
    return ESP_OK;
}

esp_err_t zone_config_set_target_count(uint8_t id, uint8_t count)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    zone_config_t *zone = zone_config_get(id);
    if (zone == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    zone->target_count = count;
    return ESP_OK;
}
