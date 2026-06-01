/**
 * @file file_manager.h
 * @brief 文件管理模块接口
 * 
 * 提供 FATFS 文件系统的管理功能，包括：
 * - 文件上传/删除/列表/下载
 * - 分区信息查询
 * - 目录管理
 */

#ifndef FILE_MANAGER_H
#define FILE_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 文件信息结构
 */
typedef struct {
    char name[256];           ///< 文件名
    char path[512];           ///< 完整路径
    size_t size;              ///< 文件大小（字节）
    bool is_dir;              ///< 是否为目录
    char modified_time[32];   ///< 修改时间（暂未实现）
} file_info_t;

/**
 * @brief 文件系统信息
 */
typedef struct {
    size_t total_bytes;       ///< 总容量
    size_t used_bytes;        ///< 已用空间
    size_t free_bytes;        ///< 可用空间
    int file_count;           ///< 文件数量
    int dir_count;            ///< 目录数量
} fs_info_t;

/**
 * @brief 文件列表结果
 */
typedef struct {
    file_info_t *files;       ///< 文件数组
    int count;                ///< 文件数量
    int capacity;             ///< 数组容量
} file_list_t;

/**
 * @brief 初始化文件管理器
 * 
 * @return ESP_OK 成功
 */
esp_err_t file_manager_init(void);

/**
 * @brief 检查 FATFS 是否已挂载
 * 
 * @return true 已挂载
 */
bool file_manager_is_ready(void);

/**
 * @brief 列出指定目录下的文件
 * 
 * @param path 目录路径（如 "/storage/www"）
 * @param list 输出文件列表
 * @return ESP_OK 成功
 */
esp_err_t file_manager_list(const char *path, file_list_t *list);

/**
 * @brief 释放文件列表内存
 * 
 * @param list 文件列表
 */
void file_manager_list_free(file_list_t *list);

/**
 * @brief 上传文件
 * 
 * @param path 目标路径（如 "/storage/www/index.html"）
 * @param data 文件数据
 * @param len 数据长度
 * @return ESP_OK 成功
 */
esp_err_t file_manager_upload(const char *path, const uint8_t *data, size_t len);

/**
 * @brief 删除文件
 * 
 * @param path 文件路径
 * @return ESP_OK 成功
 */
esp_err_t file_manager_delete(const char *path);

/**
 * @brief 检查文件是否存在
 * 
 * @param path 文件路径
 * @return true 存在
 */
bool file_manager_exists(const char *path);

/**
 * @brief 获取文件大小
 * 
 * @param path 文件路径
 * @return 文件大小，-1 表示不存在
 */
long file_manager_get_size(const char *path);

/**
 * @brief 创建目录
 * 
 * @param path 目录路径
 * @return ESP_OK 成功
 */
esp_err_t file_manager_mkdir(const char *path);

/**
 * @brief 删除目录（递归）
 * 
 * @param path 目录路径
 * @return ESP_OK 成功
 */
esp_err_t file_manager_rmdir(const char *path);

/**
 * @brief 获取文件系统信息
 * 
 * @param info 输出信息
 * @return ESP_OK 成功
 */
esp_err_t file_manager_get_fs_info(fs_info_t *info);

/**
 * @brief 格式化存储分区
 * 
 * @return ESP_OK 成功
 */
esp_err_t file_manager_format(void);

/**
 * @brief 确保目录存在（递归创建）
 * 
 * @param path 目录路径
 * @return ESP_OK 成功
 */
esp_err_t file_manager_ensure_dir(const char *path);

#ifdef __cplusplus
}
#endif

#endif // FILE_MANAGER_H
