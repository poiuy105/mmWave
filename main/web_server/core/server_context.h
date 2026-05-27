/**
 * @file server_context.h
 * @brief HTTP/WebSocket server global context structure definition
 *
 * Industrial-grade refactored - unified management of server state, resources and config
 */

#ifndef SERVER_CONTEXT_H
#define SERVER_CONTEXT_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Forward declarations
struct ws_server;
struct radar_broadcast;

/**
 * @brief Server runtime statistics
 */
typedef struct {
    // HTTP statistics
    uint32_t total_requests;          // Total requests
    uint32_t active_requests;        // Current active requests
    uint32_t error_requests;         // Error requests
    uint32_t bytes_sent;             // Bytes sent
    uint32_t bytes_received;         // Bytes received

    // WebSocket statistics
    uint32_t ws_connections;         // Total WebSocket connections
    uint32_t ws_disconnections;      // Total WebSocket disconnections
    uint32_t ws_messages_sent;       // Messages sent
    uint32_t ws_messages_failed;     // Failed messages
    uint32_t ws_heartbeats_sent;     // Heartbeats sent
    uint32_t ws_timeout_disconnects; // Timeout disconnections

    // System statistics
    uint32_t uptime_seconds;         // Uptime in seconds
    uint32_t free_heap_min;          // Minimum free heap
    uint32_t free_heap_current;      // Current free heap

    // Error statistics
    uint32_t rate_limit_hits;        // Rate limit trigger count
    uint32_t validation_failures;    // Input validation failure count
    uint32_t timeout_errors;         // Timeout error count
    uint32_t memory_errors;          // Memory error count
} server_stats_t;

/**
 * @brief Server runtime state
 */
typedef enum {
    SERVER_STATE_UNINITIALIZED = 0,
    SERVER_STATE_INITIALIZED,
    SERVER_STATE_RUNNING,
    SERVER_STATE_GRACEFUL_SHUTDOWN,
    SERVER_STATE_STOPPED
} server_state_t;

/**
 * @brief Server global context
 */
typedef struct {
    // Server handles
    httpd_handle_t http_server;          // Raw HTTP server handle (httpd)
    void *http_server_obj;               // http_server_t* object (avoids circular include)

    // Internal module handles
    struct ws_server *ws_server;         // WebSocket server
    struct radar_broadcast *broadcast;   // Radar broadcast module

    // Synchronization
    SemaphoreHandle_t mutex;              // Global state protection mutex
    SemaphoreHandle_t stats_mutex;       // Statistics update mutex

    // Runtime state
    server_state_t state;                // Server state
    bool graceful_shutdown_pending;      // Graceful shutdown flag
    uint32_t shutdown_start_time;        // Shutdown start time

    // Configuration (owned copy, not a pointer to external memory)
    struct server_config *config;         // Config copy (heap-allocated)

    // Runtime statistics
    server_stats_t stats;                // Server statistics

    // Start timestamp
    uint32_t start_time;                 // System start time
} server_context_t;

/**
 * @brief Server configuration structure (defined in server_config.h)
 */
struct server_config;

/**
 * @brief Initialize server context
 * @param config Configuration pointer
 * @return esp_err_t
 */
esp_err_t server_context_init(const struct server_config *config);

/**
 * @brief Destroy server context
 * @return esp_err_t
 */
esp_err_t server_context_deinit(void);

/**
 * @brief Get server context (thread-safe)
 * @return server_context_t* Context pointer
 */
server_context_t* server_context_get(void);

/**
 * @brief Update statistics (thread-safe)
 */
void server_stats_inc_request(void);
void server_stats_dec_request(void);
void server_stats_inc_error(void);
void server_stats_inc_ws_connection(void);
void server_stats_inc_ws_disconnect(void);
void server_stats_inc_ws_message_sent(void);
void server_stats_inc_ws_message_failed(void);
void server_stats_inc_rate_limit(void);
void server_stats_inc_validation_failure(void);
void server_stats_inc_timeout(void);
void server_stats_update_heap(void);

/**
 * @brief Get statistics copy (thread-safe)
 * @param stats Output statistics structure pointer
 */
void server_stats_get_copy(server_stats_t *stats);

/**
 * @brief Check if server is running
 */
static inline bool server_is_running(void) {
    server_context_t *ctx = server_context_get();
    return ctx && ctx->state == SERVER_STATE_RUNNING;
}

/**
 * @brief Check if server is shutting down
 */
static inline bool server_is_shutting_down(void) {
    server_context_t *ctx = server_context_get();
    return ctx && (ctx->state == SERVER_STATE_GRACEFUL_SHUTDOWN ||
                   ctx->graceful_shutdown_pending);
}

#endif // SERVER_CONTEXT_H
