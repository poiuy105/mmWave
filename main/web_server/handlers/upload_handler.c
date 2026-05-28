/**
 * @file upload_handler.c
 * @brief File management HTTP handlers implementation
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
#include <errno.h>
#include <cJSON.h>

static const char *TAG = "UPLOAD";

#define UPLOAD_BUF_SIZE 2048
#define MIN_HEAP_FOR_UPLOAD 8192

// ==================== Helper functions ====================

/**
 * Send JSON response - always succeeds (even on error, sends error JSON)
 * Returns ESP_OK if response was sent, ESP_FAIL if send itself failed.
 */
static esp_err_t send_json_resp(httpd_req_t *req, int status_code, bool success, const char *message)
{
    httpd_resp_set_status(req, status_code == 200 ? "200 OK" :
                               status_code == 400 ? "400 Bad Request" :
                               status_code == 413 ? "413 Payload Too Large" :
                               status_code == 500 ? "500 Internal Server Error" : "500");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");

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

    // Fallback: minimal response
    const char *resp = success ? "{\"success\":true}" : "{\"success\":false}";
    return httpd_resp_send(req, resp, strlen(resp));
}

/**
 * Extract query param by scanning URI manually (avoids httpd_req_get_url_query_str issues)
 */
static char* get_query_param(httpd_req_t *req, const char *key)
{
    const char *uri = req->uri;
    size_t uri_len = strlen(uri);
    size_t key_len = strlen(key);

    for (size_t i = 0; i + key_len + 1 < uri_len; i++) {
        if (uri[i] == '?' && strncmp(uri + i + 1, key, key_len) == 0 && uri[i + key_len + 1] == '=') {
            size_t start = i + key_len + 2;
            size_t end = start;
            while (end < uri_len && uri[end] != '&' && uri[end] != '#') end++;
            size_t val_len = end - start;
            if (val_len == 0) return NULL;
            char *result = malloc(val_len + 1);
            if (result) {
                memcpy(result, uri + start, val_len);
                result[val_len] = '\0';
            }
            return result;
        }
    }
    return NULL;
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
 */
static bool url_decode_inplace(char *str)
{
    char *src = str;
    char *dst = str;
    while (*src) {
        if (*src == '%' && src[1] && src[2] && is_hex_char(src[1]) && is_hex_char(src[2])) {
            char hex[3] = { src[1], src[2], '\0' };
            char decoded = (char)strtol(hex, NULL, 16);
            if (decoded == '\0') return false;
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

static esp_err_t upload_page_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Serving upload page");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, upload_page_html, strlen(upload_page_html));
}

/**
 * POST /api/files/upload
 *
 * Query params: path (target file path, e.g. /storage/www/index.html)
 * Body: raw binary data
 *
 * Strategy: streaming write (no large malloc)
 */
static esp_err_t api_upload_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "=== upload handler called, free_heap=%lu, content_len=%lu ===",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)req->content_len);

    // Check context
    server_context_t *ctx = server_context_get();
    if (!ctx || !ctx->config) {
        ESP_LOGE(TAG, "Server context is NULL");
        send_json_resp(req, 500, false, "Server not initialized");
        return ESP_OK;  // Response sent, don't return FAIL
    }

    ESP_LOGI(TAG, "Server context OK, heap=%lu", (unsigned long)esp_get_free_heap_size());

    // Get path from query string
    char *path = get_query_param(req, "path");
    ESP_LOGI(TAG, "get_query_param returned: %s", path ? path : "NULL");

    if (!path) {
        ESP_LOGW(TAG, "Missing path parameter");
        send_json_resp(req, 400, false, "Missing 'path' parameter");
        return ESP_OK;
    }

    if (!url_decode_inplace(path)) {
        ESP_LOGW(TAG, "Invalid URL encoding in path");
        send_json_resp(req, 400, false, "Invalid URL encoding");
        free(path);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Upload: path=%s, content_len=%lu, heap=%lu",
             path, (unsigned long)req->content_len, (unsigned long)esp_get_free_heap_size());

    // Validate path
    validation_result_t vresult = validate_file_path(path);
    if (vresult != VALIDATION_OK) {
        ESP_LOGW(TAG, "Path validation failed: %s", validation_result_str(vresult));
        send_json_resp(req, 400, false, validation_result_str(vresult));
        free(path);
        return ESP_OK;
    }

    // Check content length
    size_t content_length = req->content_len;
    if (content_length == 0) {
        ESP_LOGW(TAG, "Empty request body");
        send_json_resp(req, 400, false, "Empty request body");
        free(path);
        return ESP_OK;
    }

    // Check upload size limit
    uint32_t max_size = ctx->config->max_upload_size;
    if (max_size == 0) max_size = 102400;
    if (content_length > max_size) {
        ESP_LOGW(TAG, "File too large: %lu > %lu", (unsigned long)content_length, (unsigned long)max_size);
        send_json_resp(req, 413, false, "File too large");
        free(path);
        return ESP_OK;
    }

    // Check heap before file operations
    size_t heap_before = esp_get_free_heap_size();
    ESP_LOGI(TAG, "Heap before file ops: %lu", (unsigned long)heap_before);

    // Ensure parent directory exists
    esp_err_t dir_ret = file_manager_ensure_dir(path);
    ESP_LOGI(TAG, "file_manager_ensure_dir returned: %d, heap=%lu",
             dir_ret, (unsigned long)esp_get_free_heap_size());

    // Open file for writing
    FILE *file = fopen(path, "wb");
    if (!file) {
        ESP_LOGE(TAG, "fopen failed: %s (errno=%d)", path, errno);
        send_json_resp(req, 500, false, "Failed to create file");
        free(path);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "File opened, streaming upload...");

    // Stream: read chunks and write directly
    uint8_t buf[UPLOAD_BUF_SIZE];
    size_t total_written = 0;
    esp_err_t ret = ESP_OK;

    while (total_written < content_length) {
        size_t remaining = content_length - total_written;
        int to_read = (remaining > UPLOAD_BUF_SIZE) ? UPLOAD_BUF_SIZE : (int)remaining;

        int received = httpd_req_recv(req, (char *)buf, to_read);
        if (received <= 0) {
            ESP_LOGE(TAG, "recv failed at offset %lu (recv=%d, errno=%d)",
                     (unsigned long)total_written, received, errno);
            ret = ESP_FAIL;
            break;
        }

        size_t written = fwrite(buf, 1, received, file);
        if (written != (size_t)received) {
            ESP_LOGE(TAG, "fwrite failed: wrote %zu of %d", written, received);
            ret = ESP_FAIL;
            break;
        }

        total_written += received;

        // Log progress every 8192 bytes
        if (total_written % 8192 == 0 || total_written == content_length) {
            ESP_LOGI(TAG, "Upload progress: %lu/%lu bytes",
                     (unsigned long)total_written, (unsigned long)content_length);
        }
    }

    fclose(file);

    size_t heap_after = esp_get_free_heap_size();
    ESP_LOGI(TAG, "Upload complete: wrote %lu bytes, heap: %lu -> %lu, ret=%d",
             (unsigned long)total_written,
             (unsigned long)heap_before,
             (unsigned long)heap_after,
             ret);

    if (ret != ESP_OK) {
        remove(path);
        ESP_LOGE(TAG, "Upload failed, removed partial file");
        send_json_resp(req, 500, false, "Upload failed during transfer");
    } else {
        ESP_LOGI(TAG, "Upload SUCCESS: %s (%lu bytes)", path, (unsigned long)total_written);
        send_json_resp(req, 200, true, "File uploaded successfully");
    }

    free(path);
    return ESP_OK;  // Always return ESP_OK after sending response
}

/**
 * GET /api/files/list
 */
static esp_err_t api_file_list_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "=== file list handler called ===");

    char *path_param = get_query_param(req, "path");
    char *path = path_param ? path_param : strdup("/storage/www");

    if (!url_decode_inplace(path)) {
        send_json_resp(req, 400, false, "Invalid URL encoding");
        free(path);
        free(path_param);
        return ESP_OK;
    }

    if (strncmp(path, "/storage/", 9) != 0) {
        send_json_resp(req, 403, false, "Access denied");
        free(path);
        free(path_param);
        return ESP_OK;
    }

    file_list_t list;
    file_manager_list(path, &list);

    cJSON *root = cJSON_CreateObject();
    cJSON *files_array = cJSON_CreateArray();
    cJSON_AddStringToObject(root, "path", path);
    cJSON_AddItemToObject(root, "files", files_array);
    cJSON_AddNumberToObject(root, "count", list.count);

    for (int i = 0; i < list.count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", list.files[i].name);
        cJSON_AddStringToObject(item, "path", list.files[i].path);
        cJSON_AddNumberToObject(item, "size", list.files[i].size);
        cJSON_AddBoolToObject(item, "is_dir", list.files[i].is_dir);
        cJSON_AddItemToArray(files_array, item);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, json, strlen(json));
        free(json);
    }

    file_manager_list_free(&list);
    free(path);
    free(path_param);
    return ESP_OK;
}

/**
 * DELETE /api/files/delete
 */
static esp_err_t api_file_delete_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "=== delete handler called ===");

    char *path = get_query_param(req, "path");
    if (!path) {
        send_json_resp(req, 400, false, "Missing 'path' parameter");
        return ESP_OK;
    }

    if (!url_decode_inplace(path)) {
        send_json_resp(req, 400, false, "Invalid URL encoding");
        free(path);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Delete request: %s", path);

    if (strncmp(path, "/storage/", 9) != 0) {
        send_json_resp(req, 403, false, "Access denied");
        free(path);
        return ESP_OK;
    }

    esp_err_t ret = file_manager_delete(path);
    ESP_LOGI(TAG, "file_manager_delete returned: %d", ret);

    send_json_resp(req, ret == ESP_OK ? 200 : 500, ret == ESP_OK, ret == ESP_OK ? "File deleted" : "Delete failed");
    free(path);
    return ESP_OK;
}

/**
 * GET /api/fs/info
 */
static esp_err_t api_fs_info_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "=== fs info handler called ===");

    fs_info_t info;
    file_manager_get_fs_info(&info);

    cJSON *root = cJSON_CreateObject();
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
    }
    return ESP_OK;
}

/**
 * POST /api/fs/format
 */
static esp_err_t api_fs_format_handler(httpd_req_t *req)
{
    ESP_LOGW(TAG, "=== format handler called ===");
    esp_err_t ret = file_manager_format();
    send_json_resp(req, ret == ESP_OK ? 200 : 500, ret == ESP_OK,
                   ret == ESP_OK ? "Storage formatted" : "Format failed");
    return ESP_OK;
}

// ==================== Registration ====================

esp_err_t upload_handler_register(httpd_handle_t server)
{
    if (!server) return ESP_ERR_INVALID_ARG;

    httpd_uri_t uris[] = {
        { .uri = "/upload",             .method = HTTP_GET,    .handler = upload_page_handler,   .user_ctx = NULL },
        { .uri = "/api/files/upload",   .method = HTTP_POST,   .handler = api_upload_handler,   .user_ctx = NULL },
        { .uri = "/api/files/list",     .method = HTTP_GET,    .handler = api_file_list_handler,.user_ctx = NULL },
        { .uri = "/api/files/delete",   .method = HTTP_DELETE, .handler = api_file_delete_handler,.user_ctx = NULL },
        { .uri = "/api/fs/info",        .method = HTTP_GET,    .handler = api_fs_info_handler,  .user_ctx = NULL },
        { .uri = "/api/fs/format",      .method = HTTP_POST,   .handler = api_fs_format_handler,.user_ctx = NULL },
    };

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        esp_err_t r = httpd_register_uri_handler(server, &uris[i]);
        if (r != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register %s: %s", uris[i].uri, esp_err_to_name(r));
            return r;
        }
        ESP_LOGI(TAG, "Registered: %s (%s)", uris[i].uri,
                 uris[i].method == HTTP_GET ? "GET" :
                 uris[i].method == HTTP_POST ? "POST" : "DELETE");
    }

    ESP_LOGI(TAG, "All upload handlers registered");
    return ESP_OK;
}
