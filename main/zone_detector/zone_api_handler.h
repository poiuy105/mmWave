#ifndef ZONE_API_HANDLER_H
#define ZONE_API_HANDLER_H

#include "esp_http_server.h"
#include "esp_err.h"

esp_err_t zone_api_get_all_handler(httpd_req_t *req);
esp_err_t zone_api_create_handler(httpd_req_t *req);
esp_err_t zone_api_update_handler(httpd_req_t *req);
esp_err_t zone_api_delete_handler(httpd_req_t *req);

#endif
