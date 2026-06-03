/**
 * @file view_config.h
 * @brief 视图配置管理 - 持久化存储
 *
 * 保存前端视图的旋转、缩放、偏移等配置到 NVS
 */

#ifndef VIEW_CONFIG_H
#define VIEW_CONFIG_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 视图配置结构体
 */
typedef struct {
    float rotation;     // 旋转角度（度），范围 -180 ~ +180
    float scale;        // 缩放比例（像素/米），范围 10 ~ 200
    float offset_x;     // 水平偏移（像素）
    float offset_y;     // 垂直偏移（像素）
    bool valid;         // 配置是否有效（已加载过）
} view_config_t;

/**
 * @brief 初始化视图配置模块（从 NVS 加载）
 * @return ESP_OK 成功
 */
esp_err_t view_config_init(void);

/**
 * @brief 获取当前视图配置
 * @param config 输出配置
 * @return ESP_OK 成功，ESP_ERR_NOT_FOUND 未配置
 */
esp_err_t view_config_get(view_config_t *config);

/**
 * @brief 保存视图配置到 NVS
 * @param config 配置数据
 * @return ESP_OK 成功
 */
esp_err_t view_config_save(const view_config_t *config);

/**
 * @brief 重置视图配置为默认值
 * @return ESP_OK 成功
 */
esp_err_t view_config_reset(void);

/**
 * @brief 检查是否有保存的配置
 * @return true 有有效配置
 */
bool view_config_has_saved(void);

#ifdef __cplusplus
}
#endif

#endif // VIEW_CONFIG_H
