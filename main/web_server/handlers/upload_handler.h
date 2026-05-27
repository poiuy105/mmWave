/**
 * @file upload_handler.h
 * @brief File management HTTP handlers
 *
 * Provides REST API endpoints for file upload, list, delete,
 * filesystem info, and storage format operations.
 */

#ifndef UPLOAD_HANDLER_H
#define UPLOAD_HANDLER_H

#include "esp_http_server.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register all file management URI handlers
 * @param server HTTP server handle
 * @return ESP_OK on success
 */
esp_err_t upload_handler_register(httpd_handle_t server);

#ifdef __cplusplus
}
#endif

#endif // UPLOAD_HANDLER_H
