/**
 * @file fatfs_init.h
 * @brief FATFS 文件系统初始化
 */

#ifndef FATFS_INIT_H
#define FATFS_INIT_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 FATFS 文件系统
 *
 * 挂载 FATFS 到 /storage 目录，并创建必要的子目录
 *
 * @return ESP_OK 成功，其他失败
 */
esp_err_t fatfs_init(void);

/**
 * @brief 卸载 FATFS 文件系统
 *
 * @return ESP_OK 成功
 */
esp_err_t fatfs_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* FATFS_INIT_H */
