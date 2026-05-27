/**
 * @file health_handler.h
 * @brief 健康检查 API 处理器
 */

#ifndef HEALTH_HANDLER_H
#define HEALTH_HANDLER_H

#include <stdint.h>
#include "esp_err.h"
#include "esp_http_server.h"

/**
 * @brief 注册健康检查 handlers
 * @param server HTTP 服务器句柄
 * @return esp_err_t
 */
esp_err_t health_handler_register(httpd_handle_t server);

/**
 * @brief 获取健康状态描述
 * @return const char* 状态字符串
 */
const char* health_get_status_string(void);

#endif // HEALTH_HANDLER_H
