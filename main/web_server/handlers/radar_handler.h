/**
 * @file radar_handler.h
 * @brief 雷达配置 API Handler
 *
 * 提供雷达安装模式、检测范围等配置的 REST API
 */

#ifndef RADAR_HANDLER_H
#define RADAR_HANDLER_H

#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 获取雷达状态
 * GET /api/radar/status
 */
esp_err_t api_radar_status_handler(httpd_req_t *req);

/**
 * @brief 获取安装模式
 * GET /api/radar/install_mode
 */
esp_err_t api_radar_install_mode_get_handler(httpd_req_t *req);

/**
 * @brief 设置安装模式
 * PUT /api/radar/install_mode
 */
esp_err_t api_radar_install_mode_put_handler(httpd_req_t *req);

/**
 * @brief 获取检测范围
 * GET /api/radar/range
 */
esp_err_t api_radar_range_get_handler(httpd_req_t *req);

/**
 * @brief 设置检测范围
 * PUT /api/radar/range
 */
esp_err_t api_radar_range_put_handler(httpd_req_t *req);

/**
 * @brief 获取视图配置
 * GET /api/view/config
 */
esp_err_t api_view_config_get_handler(httpd_req_t *req);

/**
 * @brief 保存视图配置
 * PUT /api/view/config
 */
esp_err_t api_view_config_put_handler(httpd_req_t *req);

/**
 * @brief 重置视图配置
 * DELETE /api/view/config
 */
esp_err_t api_view_config_delete_handler(httpd_req_t *req);

#ifdef __cplusplus
}
#endif

#endif // RADAR_HANDLER_H
