/**
 * @file security_headers.h
 * @brief HTTP 安全响应头设置
 */

#ifndef SECURITY_HEADERS_H
#define SECURITY_HEADERS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_http_server.h"

/**
 * @brief 安全头配置
 */
typedef struct {
    bool x_content_type_options;    // X-Content-Type-Options
    bool x_frame_options;          // X-Frame-Options
    bool x_xss_protection;          // X-XSS-Protection
    bool strict_transport_security; // HSTS
    bool content_security_policy;   // CSP
    const char *csp_policy;         // CSP 策略字符串
} security_headers_config_t;

/**
 * @brief 默认安全头配置
 */
extern const security_headers_config_t SECURITY_HEADERS_DEFAULT;

/**
 * @brief 设置安全响应头
 * @param req HTTP 请求
 * @param config 配置（NULL 使用默认）
 */
void security_headers_set(httpd_req_t *req, const security_headers_config_t *config);

/**
 * @brief 设置 CORS 头
 * @param req HTTP 请求
 * @param allow_origin 允许的源（NULL 表示不设置）
 */
void security_headers_set_cors(httpd_req_t *req, const char *allow_origin);

/**
 * @brief 初始化默认安全配置
 */
void security_headers_init_default(void);

/**
 * @brief 获取当前安全配置
 */
const security_headers_config_t* security_headers_get_config(void);

/**
 * @brief 更新安全配置
 */
void security_headers_set_config(const security_headers_config_t *config);

#endif // SECURITY_HEADERS_H
