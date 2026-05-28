/**
 * @file upload_handler.c
 * @brief File management HTTP handlers implementation
 *
 * Endpoints:
 *   GET  /upload          - File manager web page
 *   POST /api/files/upload - Upload file (raw body, streamed to disk)
 *   GET  /api/files/list   - List files in directory
 *   DELETE /api/files/delete - Delete a file
 *   GET  /api/fs/info      - Filesystem info
 *   POST /api/fs/format    - Format storage
 */

#include "upload_handler.h"
#include "upload_page.h"
#include "file_manager.h"
#include "security/input_validator.h"
#include "security/security_headers.h"
#include "server_context.h"
#include "server_config.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdio.h>
#include <cJSON.h>

static const char *TAG = "UPLOAD";

// Upload chunk size - small to avoid memory pressure on ESP32-C3
#define UPLOAD_BUF_SIZE 2048

// Minimum heap required to send a JSON response
#define MIN_HEAP_FOR_JSON 2048

// ==================== Helper functions ====================

/**
 * Send JSON response with memory fallback and CORS headers.
 * If heap is too low for cJSON, send a minimal fixed-string JSON.
 * Returns ESP_OK if response was sent, ESP_FAIL otherwise.
 */
static esp_err_t send_json_resp(httpd_req_t *req, bool success, const char *message)
{
    httpd_resp_set_type(req, "application/json");
    
    // Add CORS headers for cross-origin requests
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");

    // If enough heap, use cJSON for proper formatting
    if (esp_get_free_heap_size() >= MIN_HEAP_FOR_JSON) {
        cJSON *root = cJSON_CreateObject();
        if (root) {
            cJSON_AddBoolToObject(root, "success", success);
            if (message) {
                cJSON_AddStringToObject(root, "message", message);
            }
            char *json = cJSON_PrintUnformatted(root);
            cJSON_Delete(root);
            if (json) {
                esp_err_t ret = httpd_resp_send(req, json, strlen(json));
                free(json);
                return ret;
            }
        }
    }

    // Fallback: send minimal fixed JSON string (no heap allocation needed)
    ESP_LOGW(TAG, "Low heap (%lu bytes), using fallback JSON response",
             (unsigned long)esp_get_free_heap_size());
    const char *resp = success
        ? "{\"success\":true,\"message\":\"ok\"}"
        : "{\"success\":false,\"message\":\"error\"}";
    return httpd_resp_send(req, resp, strlen(resp));
}

/**
 * Extract query parameter value from URI
 * Returns NULL if parameter not found (caller must free)
 */
static char* get_query_param(httpd_req_t *req, const char *key)
{
    char buf[512];
    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > sizeof(buf)) {
        buf_len = sizeof(buf);
    }

    if (httpd_req_get_url_query_str(req, buf, buf_len) != ESP_OK) {
        return NULL;
    }

    char param[256];
    int param_len = httpd_query_key_value(buf, key, param, sizeof(param));
    if (param_len <= 0) {
        return NULL;
    }

    char *result = malloc(param_len + 1);
    if (result) {
        memcpy(result, param, param_len);
        result[param_len] = '\0';
    }
    return result;
}

/**
 * Check if a character is a valid hex digit
 */
static bool is_hex_char(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/**
 * URL-decode a string in-place
 * Returns false if invalid encoding is detected
 */
static bool url_decode_inplace(char *str)
{
    char *src = str;
    char *dst = str;

    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            if (!is_hex_char(src[1]) || !is_hex_char(src[2])) {
                return false;
            }
            char hex[3] = { src[1], src[2], '\0' };
            char decoded = (char)strtol(hex, NULL, 16);
            if (decoded == '\0') {
                return false;
            }
            *dst++ = decoded;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    return true;
}

// ==================== Handlers ====================

/**
 * GET /upload - Serve the file manager web page
 */
static esp_err_t upload_page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, upload_page_html, strlen(upload_page_html));
}

/**
 * POST /api/files/upload - Upload a file (streamed to disk)
 *
 * Query params: path (target file path, e.g. /storage/www/index.html)
 * Body: raw file data
 *
 * Uses streaming: reads 2KB chunks from HTTP request and writes
 * directly to file, avoiding large heap allocations.
 */
static esp_err_t api_upload_handler(httpd_req_t *req)
{
    server_context_t *ctx = server_context_get();
    if (!ctx || !ctx->config) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Server not initialized");
    }

    // Get target path from query string
    char *path = get_query_param(req, "path");
    if (!path) {
        send_json_resp(req, false, "Missing 'path' parameter");
        return ESP_FAIL;
    }

    if (!url_decode_inplace(path)) {
        send_json_resp(req, false, "Invalid URL encoding");
        free(path);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Upload request: path=%s, size=%lu, free_heap=%lu",
             path, (unsigned long)req->content_len,
             (unsigned long)esp_get_free_heap_size());

    // Check minimum heap before starting upload (need ~8KB for FATFS buffers + operations)
    if (esp_get_free_heap_size() < 8192) {
        ESP_LOGW(TAG, "Insufficient heap for upload: %lu bytes",
                 (unsigned long)esp_get_free_heap_size());
        send_json_resp(req, false, "Server busy, insufficient memory");
        free(path);
        return ESP_FAIL;
    }

    // Validate the file path
    validation_result_t vresult = validate_file_path(path);
    if (vresult != VALIDATION_OK) {
        ESP_LOGW(TAG, "Upload path validation failed: %s", validation_result_str(vresult));
        send_json_resp(req, false, validation_result_str(vresult));
        free(path);
        return ESP_FAIL;
    }

    size_t content_length = req->content_len;
    if (content_length == 0) {
        send_json_resp(req, false, "Empty request body");
        free(path);
        return ESP_FAIL;
    }

    uint32_t max_size = ctx->config->max_upload_size;
    if (max_size == 0) {
        max_size = 102400; // Default 100KB
    }

    if (content_length > max_size) {
        ESP_LOGW(TAG, "Upload too large: %lu bytes (max=%lu)",
                 (unsigned long)content_length, (unsigned long)max_size);
        send_json_resp(req, false, "File too large");
        free(path);
        return ESP_FAIL;
    }

    // Ensure parent directory exists
    file_manager_ensure_dir(path);

    // Stream: open file first, then read chunks and write directly
    FILE *file = fopen(path, "wb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", path);
        send_json_resp(req, false, "Failed to create file");
        free(path);
        return ESP_FAIL;
    }

    uint8_t buf[UPLOAD_BUF_SIZE];
    size_t total_written = 0;
    esp_err_t ret = ESP_OK;

    while (total_written < content_length) {
        size_t remaining = content_length - total_written;
        int to_read = (remaining > UPLOAD_BUF_SIZE) ? UPLOAD_BUF_SIZE : (int)remaining;
        int received = httpd_req_recv(req, (char *)buf, to_read);

        if (received <= 0) {
            ESP_LOGE(TAG, "Upload recv error at offset %lu", (unsigned long)total_written);
            ret = ESP_FAIL;
            break;
        }

        size_t written = fwrite(buf, 1, received, file);
        if (written != (size_t)received) {
            ESP_LOGE(TAG, "File write error at offset %lu", (unsigned long)total_written);
            ret = ESP_FAIL;
            break;
        }

        total_written += received;
    }

    fclose(file);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "File uploaded: %s (%lu bytes), free heap=%lu",
                 path, (unsigned long)total_written,
                 (unsigned long)esp_get_free_heap_size());
    } else {
        // Clean up partial file on failure
        remove(path);
        ESP_LOGE(TAG, "Upload failed, partial file removed: %s", path);
    }

    // Send response - use fallback if heap is low
    // Note: send_json_resp calls httpd_resp_send internally
    esp_err_t send_ret = send_json_resp(req, ret == ESP_OK,
                   ret == ESP_OK ? "File uploaded successfully" : "Upload failed");

    free(path);
    
    // Return ESP_OK to prevent HTTPD from sending another error response
    // The response has already been sent by send_json_resp
    return (send_ret == ESP_OK) ? ESP_OK : ret;
}

/**
 * GET /api/files/list - List files in a directory
 */
static esp_err_t api_file_list_handler(httpd_req_t *req)
{
    char *path = get_query_param(req, "path");
    if (!path) {
        path = strdup("/storage/www");
    }

    if (!url_decode_inplace(path)) {
        send_json_resp(req, false, "Invalid URL encoding");
        free(path);
        return ESP_FAIL;
    }

    validation_result_t vresult = validate_file_path(path);
    if (vresult != VALIDATION_OK) {
        send_json_resp(req, false, "Invalid path");
        free(path);
        return ESP_FAIL;
    }

    if (strncmp(path, "/storage/", 9) != 0) {
        send_json_resp(req, false, "Access denied");
        free(path);
        return ESP_FAIL;
    }

    file_list_t list;
    esp_err_t ret = file_manager_list(path, &list);

    if (ret != ESP_OK) {
        send_json_resp(req, false, "Failed to list files");
        free(path);
        return ESP_FAIL;
    }

    // Build JSON response
    cJSON *root = cJSON_CreateObject();
    if (root) {
        cJSON_AddStringToObject(root, "path", path);

        cJSON *files_array = cJSON_CreateArray();
        if (files_array) {
            for (int i = 0; i < list.count; i++) {
                cJSON *item = cJSON_CreateObject();
                if (item) {
                    cJSON_AddStringToObject(item, "name", list.files[i].name);
                    cJSON_AddStringToObject(item, "path", list.files[i].path);
                    cJSON_AddNumberToObject(item, "size", list.files[i].size);
                    cJSON_AddBoolToObject(item, "is_dir", list.files[i].is_dir);
                    cJSON_AddItemToArray(files_array, item);
                }
            }
        }
        cJSON_AddItemToObject(root, "files", files_array);
        cJSON_AddNumberToObject(root, "count", list.count);

        char *json = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (json) {
            httpd_resp_set_type(req, "application/json");
            httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
            httpd_resp_send(req, json, strlen(json));
            free(json);
        } else {
            send_json_resp(req, false, "Out of memory");
        }
    } else {
        send_json_resp(req, false, "Out of memory");
    }

    file_manager_list_free(&list);
    free(path);
    return ESP_OK;
}

/**
 * DELETE /api/files/delete - Delete a file
 */
static esp_err_t api_file_delete_handler(httpd_req_t *req)
{
    char *path = get_query_param(req, "path");
    if (!path) {
        send_json_resp(req, false, "Missing 'path' parameter");
        return ESP_FAIL;
    }

    if (!url_decode_inplace(path)) {
        send_json_resp(req, false, "Invalid URL encoding");
        free(path);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Delete file: %s", path);

    validation_result_t vresult = validate_file_path(path);
    if (vresult != VALIDATION_OK) {
        send_json_resp(req, false, validation_result_str(vresult));
        free(path);
        return ESP_FAIL;
    }

    if (strcmp(path, "/storage") == 0 ||
        strcmp(path, "/storage/www") == 0 ||
        strcmp(path, "/storage/logs") == 0 ||
        strcmp(path, "/storage/config") == 0) {
        send_json_resp(req, false, "Cannot delete system directory");
        free(path);
        return ESP_FAIL;
    }

    esp_err_t ret = file_manager_delete(path);
    esp_err_t send_ret = send_json_resp(req, ret == ESP_OK,
                   ret == ESP_OK ? "File deleted" : "Delete failed");

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "File deleted: %s", path);
    } else {
        ESP_LOGE(TAG, "Delete failed: %s - %s", path, esp_err_to_name(ret));
    }

    free(path);
    return (send_ret == ESP_OK) ? ESP_OK : ret;
}

/**
 * GET /api/fs/info - Get filesystem information
 */
static esp_err_t api_fs_info_handler(httpd_req_t *req)
{
    fs_info_t info;
    esp_err_t ret = file_manager_get_fs_info(&info);

    if (ret != ESP_OK) {
        send_json_resp(req, false, "Failed to get filesystem info");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateObject();
    if (root) {
        cJSON_AddNumberToObject(root, "total_bytes", info.total_bytes);
        cJSON_AddNumberToObject(root, "used_bytes", info.used_bytes);
        cJSON_AddNumberToObject(root, "free_bytes", info.free_bytes);
        cJSON_AddNumberToObject(root, "file_count", info.file_count);
        cJSON_AddNumberToObject(root, "dir_count", info.dir_count);

        char *json = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (json) {
            httpd_resp_set_type(req, "application/json");
            httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
            httpd_resp_send(req, json, strlen(json));
            free(json);
        } else {
            send_json_resp(req, false, "Out of memory");
        }
    } else {
        send_json_resp(req, false, "Out of memory");
    }

    return ESP_OK;
}

/**
 * POST /api/fs/format - Format storage (clear www directory)
 */
static esp_err_t api_fs_format_handler(httpd_req_t *req)
{
    ESP_LOGW(TAG, "Storage format requested!");

    esp_err_t ret = file_manager_format();
    esp_err_t send_ret = send_json_resp(req, ret == ESP_OK,
                   ret == ESP_OK ? "Storage formatted" : "Format failed");

    return (send_ret == ESP_OK) ? ESP_OK : ret;
}

// ==================== Registration ====================

esp_err_t upload_handler_register(httpd_handle_t server)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret;

    httpd_uri_t uris[] = {
        { .uri = "/upload",            .method = HTTP_GET,    .handler = upload_page_handler,    .user_ctx = NULL },
        { .uri = "/api/files/upload",  .method = HTTP_POST,   .handler = api_upload_handler,    .user_ctx = NULL },
        { .uri = "/api/files/list",    .method = HTTP_GET,    .handler = api_file_list_handler, .user_ctx = NULL },
        { .uri = "/api/files/delete",  .method = HTTP_DELETE, .handler = api_file_delete_handler,.user_ctx = NULL },
        { .uri = "/api/fs/info",       .method = HTTP_GET,    .handler = api_fs_info_handler,   .user_ctx = NULL },
        { .uri = "/api/fs/format",     .method = HTTP_POST,   .handler = api_fs_format_handler, .user_ctx = NULL },
    };

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        ret = httpd_register_uri_handler(server, &uris[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register %s", uris[i].uri);
            return ret;
        }
    }

    ESP_LOGI(TAG, "Upload handlers registered (6 endpoints)");
    return ESP_OK;
}
