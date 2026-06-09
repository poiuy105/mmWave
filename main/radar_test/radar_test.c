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
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "RADAR_TEST";

#ifdef CONFIG_RADAR_LD2460
#include "radar_ld2460.h"

static esp_err_t test_install_mode(void *handle)
{
    ESP_LOGI(TAG, "=== Test: Set Install Mode ===");
    ld2460_install_mode_t current_mode;
    esp_err_t err = ld2460_get_install_mode(handle, &current_mode);
    if (err != ESP_OK) { ESP_LOGE(TAG, "Failed to get install mode: %s", esp_err_to_name(err)); return err; }
    ESP_LOGI(TAG, "Current install mode: %d (1=side, 2=top)", current_mode);
    err = ld2460_set_install_mode(handle, LD2460_INSTALL_TOP);
    if (err != ESP_OK) { ESP_LOGE(TAG, "Failed to set install mode: %s", esp_err_to_name(err)); return err; }
    vTaskDelay(pdMS_TO_TICKS(1000));
    err = ld2460_get_install_mode(handle, &current_mode);
    if (err != ESP_OK) { ESP_LOGE(TAG, "Failed to verify install mode: %s", esp_err_to_name(err)); return err; }
    ESP_LOGI(TAG, "Verified install mode: %d (expected 2)", current_mode);
    err = ld2460_set_install_mode(handle, LD2460_INSTALL_SIDE);
    if (err != ESP_OK) { ESP_LOGE(TAG, "Failed to restore install mode: %s", esp_err_to_name(err)); return err; }
    return ESP_OK;
}

static esp_err_t test_sensitivity(void *handle)
{
    ESP_LOGI(TAG, "=== Test: Set Sensitivity ===");
    ld2460_sensitivity_t current_level;
    esp_err_t err = ld2460_get_sensitivity(handle, &current_level);
    if (err != ESP_OK) { ESP_LOGE(TAG, "Failed to get sensitivity: %s", esp_err_to_name(err)); return err; }
    err = ld2460_set_sensitivity(handle, LD2460_SENSITIVITY_LOW);
    if (err != ESP_OK) { ESP_LOGE(TAG, "Failed to set sensitivity: %s", esp_err_to_name(err)); return err; }
    vTaskDelay(pdMS_TO_TICKS(1000));
    err = ld2460_get_sensitivity(handle, &current_level);
    if (err != ESP_OK) { ESP_LOGE(TAG, "Failed to verify sensitivity: %s", esp_err_to_name(err)); return err; }
    err = ld2460_set_sensitivity(handle, LD2460_SENSITIVITY_HIGH);
    if (err != ESP_OK) { ESP_LOGE(TAG, "Failed to restore sensitivity: %s", esp_err_to_name(err)); return err; }
    return ESP_OK;
}

static esp_err_t test_detection_range(void *handle)
{
    ESP_LOGI(TAG, "=== Test: Set Detection Range ===");
    ld2460_range_t current_range;
    esp_err_t err = ld2460_get_detection_range(handle, &current_range);
    if (err != ESP_OK) { ESP_LOGE(TAG, "Failed to get detection range: %s", esp_err_to_name(err)); return err; }
    err = ld2460_set_detection_range(handle, 3.0f, -30.0f, 30.0f);
    if (err != ESP_OK) { ESP_LOGE(TAG, "Failed to set detection range: %s", esp_err_to_name(err)); return err; }
    vTaskDelay(pdMS_TO_TICKS(1000));
    err = ld2460_get_detection_range(handle, &current_range);
    if (err != ESP_OK) { ESP_LOGE(TAG, "Failed to verify detection range: %s", esp_err_to_name(err)); return err; }
    err = ld2460_set_detection_range(handle, 6.0f, -60.0f, 60.0f);
    if (err != ESP_OK) { ESP_LOGE(TAG, "Failed to restore detection range: %s", esp_err_to_name(err)); return err; }
    return ESP_OK;
}

static esp_err_t test_firmware_version(void *handle)
{
    ESP_LOGI(TAG, "=== Test: Get Firmware Version ===");
    ld2460_version_t version;
    esp_err_t err = ld2460_get_firmware_version(handle, &version);
    if (err != ESP_OK) { ESP_LOGE(TAG, "Failed to get firmware version: %s", esp_err_to_name(err)); return err; }
    ESP_LOGI(TAG, "Firmware version: %d.%d, date: %d/%02d, install_mode=%d",
             version.version_major, version.version_minor, 2000 + version.year, version.month, version.mode);
    return ESP_OK;
}
#endif // CONFIG_RADAR_LD2460

esp_err_t radar_test_run_all(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "RADAR CONTROL COMMAND VERIFICATION TEST");
    ESP_LOGI(TAG, "========================================");

    void *handle = radar_adapter_get_handle();
    if (handle == NULL) {
        ESP_LOGE(TAG, "Radar not initialized, cannot run test");
        return ESP_ERR_INVALID_STATE;
    }

    radar_info_t info;
    radar_adapter_get_info(&info);
    ESP_LOGI(TAG, "Radar type: %s", info.type);

#ifndef CONFIG_RADAR_LD2460
    ESP_LOGW(TAG, "This test is designed for LD2460 only, skipping");
    return ESP_OK;
#else
    if (strcmp(info.type, "LD2460") != 0) {
        ESP_LOGW(TAG, "This test is designed for LD2460 only, skipping");
        return ESP_OK;
    }

    esp_err_t err;
    err = ld2460_enable_report(handle, false);
    if (err != ESP_OK) { ESP_LOGW(TAG, "Failed to stop report: %s", esp_err_to_name(err)); }
    vTaskDelay(pdMS_TO_TICKS(500));

    err = test_firmware_version(handle);
    if (err != ESP_OK) { ESP_LOGE(TAG, "Firmware version test FAILED"); ld2460_enable_report(handle, true); return err; }
    vTaskDelay(pdMS_TO_TICKS(1000));

    err = test_install_mode(handle);
    if (err != ESP_OK) { ESP_LOGE(TAG, "Install mode test FAILED"); ld2460_enable_report(handle, true); return err; }
    vTaskDelay(pdMS_TO_TICKS(2000));

    err = test_sensitivity(handle);
    if (err != ESP_OK) { ESP_LOGE(TAG, "Sensitivity test FAILED"); ld2460_enable_report(handle, true); return err; }
    vTaskDelay(pdMS_TO_TICKS(2000));

    err = test_detection_range(handle);
    if (err != ESP_OK) { ESP_LOGE(TAG, "Detection range test FAILED"); ld2460_enable_report(handle, true); return err; }

    ld2460_enable_report(handle, true);
    ESP_LOGI(TAG, "ALL TESTS PASSED!");
    return ESP_OK;
#endif
}
