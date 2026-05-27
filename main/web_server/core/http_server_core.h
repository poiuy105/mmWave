/**
 * @file http_server_core.h
 * @brief HTTP server core interface
 *
 * Responsibilities:
 * - HTTP server creation and destruction
 * - URI handlers registration
 * - Graceful shutdown support
 */

#ifndef HTTP_SERVER_CORE_H
#define HTTP_SERVER_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"
#include "server_context.h"
#include "server_config.h"

// ==================== Forward declarations ====================

typedef struct http_server http_server_t;

// ==================== HTTP Server API ====================

/**
 * @brief Create HTTP server instance
 * @param config Server configuration
 * @return http_server_t* Server handle
 */
http_server_t* http_server_create(const server_config_t *config);

/**
 * @brief Start HTTP server instance
 * @param server Server handle
 * @return esp_err_t
 */
esp_err_t http_server_core_start(http_server_t *server);

/**
 * @brief Stop HTTP server instance
 * @param server Server handle
 * @return esp_err_t
 */
esp_err_t http_server_core_stop(http_server_t *server);

/**
 * @brief Graceful stop (wait for existing requests to complete)
 * @param server Server handle
 * @param timeout_ms Timeout in milliseconds
 * @return esp_err_t
 */
esp_err_t http_server_core_graceful_stop(http_server_t *server, uint32_t timeout_ms);

/**
 * @brief Destroy HTTP server
 * @param server Server handle
 */
void http_server_core_destroy(http_server_t *server);

/**
 * @brief Get HTTP server handle
 * @param server Server handle
 * @return httpd_handle_t
 */
httpd_handle_t http_server_core_get_handle(http_server_t *server);

/**
 * @brief Check if server is running
 * @param server Server handle
 * @return true Running
 * @return false Not running
 */
bool http_server_core_is_running(http_server_t *server);

/**
 * @brief Get server statistics
 * @param server Server handle
 * @param total_requests Total requests
 * @param active_requests Active requests
 * @param error_requests Error requests
 */
void http_server_core_get_stats(http_server_t *server,
                          uint32_t *total_requests,
                          uint32_t *active_requests,
                          uint32_t *error_requests);

#endif // HTTP_SERVER_CORE_H
