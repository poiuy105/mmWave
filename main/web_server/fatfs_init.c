/**
 * @file fatfs_init.c
 * @brief FATFS filesystem initialization
 */

#include "fatfs_init.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"
#include "esp_log.h"
#include <sys/stat.h>

static const char *TAG = "FATFS";
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;
static bool s_mounted = false;

esp_err_t fatfs_init(void)
{
    if (s_mounted) {
        ESP_LOGW(TAG, "FATFS already mounted");
        return ESP_OK;
    }

    const esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 8,  // 缩减以节省 heap（实际同时打开文件很少超过 4 个）
        .allocation_unit_size = 4 * 1024
    };

    ESP_LOGI(TAG, "Mounting FATFS...");

    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(
        "/storage",
        "storage",
        &mount_config,
        &s_wl_handle
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount FATFS: %s", esp_err_to_name(err));
        return err;
    }

    s_mounted = true;
    ESP_LOGI(TAG, "FATFS mounted at /storage");

    // Create directory structure
    struct stat st;

    // Create www directory
    if (stat("/storage/www", &st) != 0) {
        if (mkdir("/storage/www", 0755) != 0) {
            ESP_LOGW(TAG, "Failed to create /storage/www");
        } else {
            ESP_LOGI(TAG, "Created /storage/www");
        }
    }

    // Create logs directory
    if (stat("/storage/logs", &st) != 0) {
        if (mkdir("/storage/logs", 0755) != 0) {
            ESP_LOGW(TAG, "Failed to create /storage/logs");
        } else {
            ESP_LOGI(TAG, "Created /storage/logs");
        }
    }

    // Create config directory
    if (stat("/storage/config", &st) != 0) {
        if (mkdir("/storage/config", 0755) != 0) {
            ESP_LOGW(TAG, "Failed to create /storage/config");
        } else {
            ESP_LOGI(TAG, "Created /storage/config");
        }
    }

    return ESP_OK;
}

esp_err_t fatfs_deinit(void)
{
    if (!s_mounted) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Unmounting FATFS...");

    esp_err_t err = esp_vfs_fat_spiflash_unmount_rw_wl("/storage", s_wl_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unmount FATFS: %s", esp_err_to_name(err));
        return err;
    }

    s_mounted = false;
    s_wl_handle = WL_INVALID_HANDLE;
    ESP_LOGI(TAG, "FATFS unmounted");

    return ESP_OK;
}
