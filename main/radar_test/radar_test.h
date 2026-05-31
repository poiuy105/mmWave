/**
 * @file radar_test.h
 * @brief 雷达控制命令底层验证测试
 */

#ifndef RADAR_TEST_H
#define RADAR_TEST_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 运行所有雷达控制测试
 *
 * 测试内容：
 * - 设置安装模式（侧装/顶装）
 * - 设置灵敏度（高/中/低）
 * - 设置检测范围（距离和角度）
 *
 * @return ESP_OK 所有测试通过
 * @return ESP_FAIL 测试失败
 */
esp_err_t radar_test_run_all(void);

#ifdef __cplusplus
}
#endif

#endif // RADAR_TEST_H