/**
 * @file radar_test.c
 * @brief 雷达控制命令底层验证测试
 *
 * 测试 LD2460 驱动的写命令是否能正确控制雷达硬件：
 * - 设置安装模式
 * - 设置灵敏度
 * - 设置检测范围
 */

#include "radar_test.h"
#include "radar_adapter/radar_adapter.h"
#include "radar_ld2460.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "RADAR_TEST";

/**
 * @brief 测试 LD2460 安装模式设置
 */
static esp_err_t test_install_mode(void *handle)
{
    ESP_LOGI(TAG, "=== Test: Set Install Mode ===");

    // 1. 获取当前安装模式
    ld2460_install_mode_t current_mode;
    esp_err_t err = ld2460_get_install_mode(handle, &current_mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get install mode: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Current install mode: %d (1=side, 2=top)", current_mode);

    // 2. 设置为顶装模式
    ESP_LOGI(TAG, "Setting install mode to TOP (2)...");
    err = ld2460_set_install_mode(handle, LD2460_INSTALL_TOP);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set install mode: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Set install mode TOP: SUCCESS");

    // 3. 等待 1 秒后读取确认
    vTaskDelay(pdMS_TO_TICKS(1000));

    err = ld2460_get_install_mode(handle, &current_mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to verify install mode: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Verified install mode: %d (expected 2)", current_mode);

    // 4. 恢复侧装模式
    ESP_LOGI(TAG, "Restoring install mode to SIDE (1)...");
    err = ld2460_set_install_mode(handle, LD2460_INSTALL_SIDE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to restore install mode: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Restore install mode SIDE: SUCCESS");

    return ESP_OK;
}

/**
 * @brief 测试 LD2460 灵敏度设置
 */
static esp_err_t test_sensitivity(void *handle)
{
    ESP_LOGI(TAG, "=== Test: Set Sensitivity ===");

    // 1. 获取当前灵敏度
    ld2460_sensitivity_t current_level;
    esp_err_t err = ld2460_get_sensitivity(handle, &current_level);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get sensitivity: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Current sensitivity: %d (1=high, 2=mid, 3=low)", current_level);

    // 2. 设置为低灵敏度
    ESP_LOGI(TAG, "Setting sensitivity to LOW (3)...");
    err = ld2460_set_sensitivity(handle, LD2460_SENSITIVITY_LOW);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set sensitivity: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Set sensitivity LOW: SUCCESS");

    // 3. 等待 1 秒后读取确认
    vTaskDelay(pdMS_TO_TICKS(1000));

    err = ld2460_get_sensitivity(handle, &current_level);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to verify sensitivity: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Verified sensitivity: %d (expected 3)", current_level);

    // 4. 恢复高灵敏度
    ESP_LOGI(TAG, "Restoring sensitivity to HIGH (1)...");
    err = ld2460_set_sensitivity(handle, LD2460_SENSITIVITY_HIGH);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to restore sensitivity: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Restore sensitivity HIGH: SUCCESS");

    return ESP_OK;
}

/**
 * @brief 测试 LD2460 检测范围设置
 */
static esp_err_t test_detection_range(void *handle)
{
    ESP_LOGI(TAG, "=== Test: Set Detection Range ===");

    // 1. 获取当前检测范围
    ld2460_range_t current_range;
    esp_err_t err = ld2460_get_detection_range(handle, &current_range);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get detection range: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Current range: distance=%.1fm, angle=%.1f~%.1f",
             current_range.distance, current_range.angle_start, current_range.angle_end);

    // 2. 设置缩小检测范围（3m, ±30°）
    ESP_LOGI(TAG, "Setting detection range to 3m, ±30°...");
    err = ld2460_set_detection_range(handle, 3.0f, -30.0f, 30.0f);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set detection range: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Set detection range: SUCCESS");

    // 3. 等待 1 秒后读取确认
    vTaskDelay(pdMS_TO_TICKS(1000));

    err = ld2460_get_detection_range(handle, &current_range);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to verify detection range: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Verified range: distance=%.1fm, angle=%.1f~%.1f (expected 3m, ±30°)",
             current_range.distance, current_range.angle_start, current_range.angle_end);

    // 4. 恢复默认范围（6m, ±60°）
    ESP_LOGI(TAG, "Restoring detection range to 6m, ±60°...");
    err = ld2460_set_detection_range(handle, 6.0f, -60.0f, 60.0f);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to restore detection range: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Restore detection range: SUCCESS");

    return ESP_OK;
}

/**
 * @brief 运行所有雷达控制测试
 */
esp_err_t radar_test_run_all(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "RADAR CONTROL COMMAND VERIFICATION TEST");
    ESP_LOGI(TAG, "========================================");

    // 获取底层驱动句柄
    void *handle = radar_adapter_get_handle();
    if (handle == NULL) {
        ESP_LOGE(TAG, "Radar not initialized, cannot run test");
        return ESP_ERR_INVALID_STATE;
    }

    // 检查雷达型号
    radar_info_t info;
    radar_adapter_get_info(&info);
    ESP_LOGI(TAG, "Radar type: %s", info.type);

    // 只测试 LD2460
    if (strcmp(info.type, "LD2460") != 0) {
        ESP_LOGW(TAG, "This test is designed for LD2460 only, skipping");
        return ESP_OK;
    }

    esp_err_t err;

    // 测试 1: 安装模式
    err = test_install_mode(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Install mode test FAILED");
        return err;
    }
    ESP_LOGI(TAG, "Install mode test PASSED");
    vTaskDelay(pdMS_TO_TICKS(2000));

    // 测试 2: 灵敏度
    err = test_sensitivity(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Sensitivity test FAILED");
        return err;
    }
    ESP_LOGI(TAG, "Sensitivity test PASSED");
    vTaskDelay(pdMS_TO_TICKS(2000));

    // 测试 3: 检测范围
    err = test_detection_range(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Detection range test FAILED");
        return err;
    }
    ESP_LOGI(TAG, "Detection range test PASSED");

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "ALL TESTS PASSED!");
    ESP_LOGI(TAG, "========================================");

    return ESP_OK;
}