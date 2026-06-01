/**
 * @file http_server.h
 * @brief HTTP 服务器接口
 */

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief 启动 HTTP 服务器
 *
 * 初始化 HTTP 服务器，注册静态文件处理和 WebSocket 处理
 *
 * @return ESP_OK 成功，其他失败
 */
esp_err_t http_server_start(void);

/**
 * @brief 停止 HTTP 服务器
 *
 * @return ESP_OK 成功
 */
esp_err_t http_server_stop(void);

/**
 * @brief 检查服务器是否运行中
 *
 * @return true 运行中
 */
bool http_server_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_SERVER_H */
