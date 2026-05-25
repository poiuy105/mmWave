/**
 * @file file_manager.c
 * @brief 文件管理模块实现
 */

#include "file_manager.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

static const char *TAG = "FILE_MGR";

// 存储句柄
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;
static bool s_initialized = false;

// 存储分区挂载点
#define STORAGE_MOUNT_POINT "/storage"

esp_err_t file_manager_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "File manager already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing file manager...");

    // FATFS 已由 fatfs_init() 挂载，这里只检查是否可用
    struct stat st;
    if (stat(STORAGE_MOUNT_POINT, &st) != 0) {
        ESP_LOGE(TAG, "Storage not mounted at %s", STORAGE_MOUNT_POINT);
        return ESP_ERR_NOT_FOUND;
    }

    // 确保 www 目录存在
    char www_path[64];
    snprintf(www_path, sizeof(www_path), "%s/www", STORAGE_MOUNT_POINT);
    if (stat(www_path, &st) != 0) {
        ESP_LOGI(TAG, "Creating www directory...");
        mkdir(www_path, 0755);
    }

    s_initialized = true;
    ESP_LOGI(TAG, "File manager initialized successfully");
    return ESP_OK;
}

bool file_manager_is_ready(void)
{
    return s_initialized;
}

esp_err_t file_manager_list(const char *path, file_list_t *list)
{
    if (!s_initialized || !path || !list) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(list, 0, sizeof(file_list_t));
    
    DIR *dir = opendir(path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    // 第一次遍历：计数
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        count++;
    }

    if (count == 0) {
        closedir(dir);
        return ESP_OK;
    }

    // 分配内存
    list->files = (file_info_t *)calloc(count, sizeof(file_info_t));
    if (!list->files) {
        closedir(dir);
        return ESP_ERR_NO_MEM;
    }
    list->capacity = count;

    // 第二次遍历：填充信息
    rewinddir(dir);
    int idx = 0;
    while ((entry = readdir(dir)) != NULL && idx < count) {
        // 跳过 . 和 ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        strncpy(list->files[idx].name, entry->d_name, sizeof(list->files[idx].name) - 1);
        snprintf(list->files[idx].path, sizeof(list->files[idx].path), "%s/%s", path, entry->d_name);
        list->files[idx].is_dir = (entry->d_type == DT_DIR);

        // 获取文件大小
        if (!list->files[idx].is_dir) {
            struct stat st;
            if (stat(list->files[idx].path, &st) == 0) {
                list->files[idx].size = st.st_size;
            }
        }

        idx++;
    }

    list->count = idx;
    closedir(dir);
    return ESP_OK;
}

void file_manager_list_free(file_list_t *list)
{
    if (list && list->files) {
        free(list->files);
        list->files = NULL;
        list->count = 0;
        list->capacity = 0;
    }
}

esp_err_t file_manager_upload(const char *path, const uint8_t *data, size_t len)
{
    if (!s_initialized || !path || !data) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Uploading file: %s (%zu bytes)", path, len);

    // 确保父目录存在
    char parent_dir[512];
    strncpy(parent_dir, path, sizeof(parent_dir) - 1);
    char *last_slash = strrchr(parent_dir, '/');
    if (last_slash && last_slash != parent_dir) {
        *last_slash = '\0';
        file_manager_ensure_dir(parent_dir);
    }

    // 写入文件
    FILE *file = fopen(path, "wb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to create file: %s (errno=%d)", path, errno);
        return ESP_ERR_NOT_FOUND;
    }

    size_t written = fwrite(data, 1, len, file);
    fclose(file);

    if (written != len) {
        ESP_LOGE(TAG, "Write failed: wrote %zu of %zu bytes", written, len);
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TAG, "File uploaded successfully: %s", path);
    return ESP_OK;
}

esp_err_t file_manager_delete(const char *path)
{
    if (!s_initialized || !path) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Deleting file: %s", path);

    if (remove(path) != 0) {
        ESP_LOGE(TAG, "Failed to delete file: %s (errno=%d)", path, errno);
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}

bool file_manager_exists(const char *path)
{
    if (!path) return false;
    
    struct stat st;
    return (stat(path, &st) == 0);
}

long file_manager_get_size(const char *path)
{
    if (!path) return -1;

    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }
    return st.st_size;
}

esp_err_t file_manager_mkdir(const char *path)
{
    if (!s_initialized || !path) {
        return ESP_ERR_INVALID_ARG;
    }

    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "Failed to create directory: %s (errno=%d)", path, errno);
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}

esp_err_t file_manager_rmdir(const char *path)
{
    if (!s_initialized || !path) {
        return ESP_ERR_INVALID_ARG;
    }

    // 递归删除目录内容
    DIR *dir = opendir(path);
    if (!dir) {
        return ESP_ERR_NOT_FOUND;
    }

    struct dirent *entry;
    char full_path[512];
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        
        if (entry->d_type == DT_DIR) {
            file_manager_rmdir(full_path);
        } else {
            remove(full_path);
        }
    }

    closedir(dir);
    
    if (rmdir(path) != 0) {
        ESP_LOGE(TAG, "Failed to remove directory: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}

esp_err_t file_manager_get_fs_info(fs_info_t *info)
{
    if (!s_initialized || !info) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(info, 0, sizeof(fs_info_t));

    // FATFS 统计信息（简化实现）
    // 注意：完整的 FATFS 统计需要使用 f_getfree 等 API
    // 这里使用简化版本
    
    // 遍历统计文件数量
    file_list_t list;
    if (file_manager_list(STORAGE_MOUNT_POINT, &list) == ESP_OK) {
        for (int i = 0; i < list.count; i++) {
            if (list.files[i].is_dir) {
                info->dir_count++;
            } else {
                info->file_count++;
                info->used_bytes += list.files[i].size;
            }
        }
        file_manager_list_free(&list);
    }

    // 分区大小（从分区表获取，这里使用固定值）
    // 实际应该从 wear leveling 层获取
    info->total_bytes = 1536 * 1024;  // 约 1.5MB
    info->free_bytes = info->total_bytes - info->used_bytes;

    return ESP_OK;
}

esp_err_t file_manager_format(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGW(TAG, "Clearing www directory...");

    // 不重新格式化分区，只删除 www 目录下的所有内容
    // 避免与 fatfs_init.c 的挂载状态冲突
    char www_path[64];
    snprintf(www_path, sizeof(www_path), "%s/www", STORAGE_MOUNT_POINT);

    // 先删除 www 目录及其内容
    file_manager_rmdir(www_path);

    // 重新创建空的 www 目录
    if (mkdir(www_path, 0755) != 0) {
        ESP_LOGE(TAG, "Failed to recreate www directory");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Storage cleared successfully");
    return ESP_OK;
}

esp_err_t file_manager_ensure_dir(const char *path)
{
    if (!path) {
        return ESP_ERR_INVALID_ARG;
    }

    // 如果目录已存在，直接返回
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        return ESP_OK;
    }

    // 递归创建父目录
    char parent[512];
    strncpy(parent, path, sizeof(parent) - 1);
    
    char *p = parent;
    while ((p = strchr(p + 1, '/')) != NULL) {
        *p = '\0';
        mkdir(parent, 0755);
        *p = '/';
    }

    // 创建最终目录
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "Failed to create directory: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}
