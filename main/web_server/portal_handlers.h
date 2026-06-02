#ifndef PORTAL_HANDLERS_H
#define PORTAL_HANDLERS_H

#include "esp_http_server.h"

// 注册所有配置门户的 URI handler
esp_err_t portal_handlers_register(httpd_handle_t server);

// 各 handler
esp_err_t portal_root_handler(httpd_req_t *req);
esp_err_t portal_scan_handler(httpd_req_t *req);
esp_err_t portal_get_config_handler(httpd_req_t *req);
esp_err_t portal_post_config_handler(httpd_req_t *req);
esp_err_t portal_get_status_handler(httpd_req_t *req);
esp_err_t portal_captive_detect_handler(httpd_req_t *req);
esp_err_t portal_captive_redirect_handler(httpd_req_t *req);

#endif
