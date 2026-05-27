/**
 * @file upload_handler.c
 * @brief File management HTTP handlers implementation
 *
 * Endpoints:
 *   GET  /upload          - File manager web page
 *   POST /api/files/upload - Upload file (raw body)
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
#include <string.h>
#include <stdio.h>
#include <cJSON.h>

static const char *TAG = "UPLOAD";

// Maximum upload buffer size (ESP32-C3 memory is limited)
#define UPLOAD_BUF_SIZE 4096

// ==================== Helper functions ====================

static void send_json_ok(httpd_req_t *req, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    if (message) {
        cJSON_AddStringToObject(root, "message", message);
    }
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    cJSON_Delete(root);
}

static void send_json_error(httpd_req_t *req, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", false);
    if (message) {
        cJSON_AddStringToObject(root, "message", message);
    }
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    cJSON_Delete(root);
}

/**
 * Extract query parameter value from URI
 * Returns NULL if parameter not found
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

    // Allocate and return a copy
    char *result = malloc(param_len + 1);
    if (result) {
        memcpy(result, param, param_len);
        result[param_len] = '\0';
    }
    return result;
}

/**
 * URL-decode a string in-place
 */
static void url_decode_inplace(char *str)
{
    char *src = str;
    char *dst = str;

    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            char hex[3] = { src[1], src[2], '\0' };
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

// ==================== Handlers ====================

/**
 * GET /upload - Serve the file manager web page
 */
static esp_err_t upload_page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, upload_page_html, strlen(upload_page_html));
}

/**
 * POST /api/files/upload - Upload a file
 *
 * Query params: path (target file path, e.g. /storage/www/index.html)
 * Body: raw file data (not multipart)
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
        send_json_error(req, "Missing 'path' parameter");
        return ESP_FAIL;
    }

    // URL-decode the path
    url_decode_inplace(path);

    ESP_LOGI(TAG, "Upload request: path=%s", path);

    // Validate the file path
    validation_result_t vresult = validate_file_path(path);
    if (vresult != VALIDATION_OK) {
        ESP_LOGW(TAG, "Upload path validation failed: %s", validation_result_str(vresult));
        send_json_error(req, validation_result_str(vresult));
        free(path);
        return ESP_FAIL;
    }

    // Check content length
    int content_length = req->content_len;
    if (content_length <= 0) {
        send_json_error(req, "Empty request body");
        free(path);
        return ESP_FAIL;
    }

    uint32_t max_size = ctx->config->max_upload_size;
    if (max_size == 0) {
        max_size = 102400; // Default 100KB
    }

    if ((uint32_t)content_length > max_size) {
        ESP_LOGW(TAG, "Upload too large: %d bytes (max=%lu)",
                 content_length, (unsigned long)max_size);
        send_json_error(req, "File too large");
        free(path);
        return ESP_FAIL;
    }

    // Read request body into buffer
    // For ESP32-C3 with limited RAM, use a reasonable buffer
    uint8_t *data = NULL;
    esp_err_t ret = ESP_OK;

    if (content_length <= UPLOAD_BUF_SIZE) {
        // Small file: read directly into stack buffer
        uint8_t buf[UPLOAD_BUF_SIZE];
        int received = httpd_req_recv(req, (char *)buf, content_length);
        if (received != content_length) {
            ESP_LOGE(TAG, "Failed to receive file data: got %d of %d", received, content_length);
            send_json_error(req, "Failed to receive file data");
            free(path);
            return ESP_FAIL;
        }
        ret = file_manager_upload(path, buf, received);
    } else {
        // Larger file: allocate heap buffer
        data = malloc(content_length);
        if (!data) {
            ESP_LOGE(TAG, "Failed to allocate %d bytes for upload", content_length);
            send_json_error(req, "Out of memory");
            free(path);
            return ESP_FAIL;
        }

        int total_received = 0;
        while (total_received < content_length) {
            int remaining = content_length - total_received;
            int to_read = (remaining > UPLOAD_BUF_SIZE) ? UPLOAD_BUF_SIZE : remaining;
            int received = httpd_req_recv(req, (char *)(data + total_received), to_read);

            if (received <= 0) {
                ESP_LOGE(TAG, "Upload recv error at offset %d", total_received);
                ret = ESP_FAIL;
                break;
            }
            total_received += received;
        }

        if (ret == ESP_OK && total_received == content_length) {
            ret = file_manager_upload(path, data, total_received);
        }
        free(data);
    }

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "File uploaded: %s (%d bytes)", path, content_length);
        send_json_ok(req, "File uploaded successfully");
    } else {
        ESP_LOGE(TAG, "Upload failed: %s", esp_err_to_name(ret));
        send_json_error(req, "Upload failed");
    }

    free(path);
    return ret;
}

/**
 * GET /api/files/list - List files in a directory
 *
 * Query params: path (directory path, e.g. /storage/www)
 */
static esp_err_t api_file_list_handler(httpd_req_t *req)
{
    // Get directory path from query string
    char *path = get_query_param(req, "path");
    if (!path) {
        path = strdup("/storage/www");
    }

    url_decode_inplace(path);
    ESP_LOGD(TAG, "List files: %s", path);

    // Security: validate the directory path to prevent path traversal
    validation_result_t vresult = validate_file_path(path);
    if (vresult != VALIDATION_OK) {
        ESP_LOGW(TAG, "List path validation failed: %s", validation_result_str(vresult));
        send_json_error(req, "Invalid path");
        free(path);
        return ESP_FAIL;
    }

    // Restrict to /storage/ prefix to prevent accessing arbitrary directories
    if (strncmp(path, "/storage/", 9) != 0) {
        ESP_LOGW(TAG, "List path outside /storage/: %s", path);
        send_json_error(req, "Access denied");
        free(path);
        return ESP_FAIL;
    }

    file_list_t list;
    esp_err_t ret = file_manager_list(path, &list);

    if (ret != ESP_OK) {
        send_json_error(req, "Failed to list files");
        free(path);
        return ESP_FAIL;
    }

    // Build JSON response
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "path", path);

    cJSON *files_array = cJSON_CreateArray();
    for (int i = 0; i < list.count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", list.files[i].name);
        cJSON_AddStringToObject(item, "path", list.files[i].path);
        cJSON_AddNumberToObject(item, "size", list.files[i].size);
        cJSON_AddBoolToObject(item, "is_dir", list.files[i].is_dir);
        cJSON_AddItemToArray(files_array, item);
    }
    cJSON_AddItemToObject(root, "files", files_array);
    cJSON_AddNumberToObject(root, "count", list.count);

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));

    free(json);
    cJSON_Delete(root);
    file_manager_list_free(&list);
    free(path);

    return ESP_OK;
}

/**
 * DELETE /api/files/delete - Delete a file
 *
 * Query params: path (file path, e.g. /storage/www/test.html)
 */
static esp_err_t api_file_delete_handler(httpd_req_t *req)
{
    // Get file path from query string
    char *path = get_query_param(req, "path");
    if (!path) {
        send_json_error(req, "Missing 'path' parameter");
        return ESP_FAIL;
    }

    url_decode_inplace(path);
    ESP_LOGI(TAG, "Delete file: %s", path);

    // Validate path
    validation_result_t vresult = validate_file_path(path);
    if (vresult != VALIDATION_OK) {
        ESP_LOGW(TAG, "Delete path validation failed: %s", validation_result_str(vresult));
        send_json_error(req, validation_result_str(vresult));
        free(path);
        return ESP_FAIL;
    }

    // Prevent deletion of critical directories
    if (strcmp(path, "/storage") == 0 ||
        strcmp(path, "/storage/www") == 0 ||
        strcmp(path, "/storage/logs") == 0 ||
        strcmp(path, "/storage/config") == 0) {
        send_json_error(req, "Cannot delete system directory");
        free(path);
        return ESP_FAIL;
    }

    esp_err_t ret = file_manager_delete(path);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "File deleted: %s", path);
        send_json_ok(req, "File deleted");
    } else {
        ESP_LOGE(TAG, "Delete failed: %s - %s", path, esp_err_to_name(ret));
        send_json_error(req, "Delete failed");
    }

    free(path);
    return ret;
}

/**
 * GET /api/fs/info - Get filesystem information
 */
static esp_err_t api_fs_info_handler(httpd_req_t *req)
{
    fs_info_t info;
    esp_err_t ret = file_manager_get_fs_info(&info);

    if (ret != ESP_OK) {
        send_json_error(req, "Failed to get filesystem info");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "total_bytes", info.total_bytes);
    cJSON_AddNumberToObject(root, "used_bytes", info.used_bytes);
    cJSON_AddNumberToObject(root, "free_bytes", info.free_bytes);
    cJSON_AddNumberToObject(root, "file_count", info.file_count);
    cJSON_AddNumberToObject(root, "dir_count", info.dir_count);

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));

    free(json);
    cJSON_Delete(root);

    return ESP_OK;
}

/**
 * POST /api/fs/format - Format storage (clear www directory)
 */
static esp_err_t api_fs_format_handler(httpd_req_t *req)
{
    ESP_LOGW(TAG, "Storage format requested!");

    esp_err_t ret = file_manager_format();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Storage formatted successfully");
        send_json_ok(req, "Storage formatted");
    } else {
        ESP_LOGE(TAG, "Format failed: %s", esp_err_to_name(ret));
        send_json_error(req, "Format failed");
    }

    return ret;
}

// ==================== Registration ====================

esp_err_t upload_handler_register(httpd_handle_t server)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret;

    // GET /upload - File manager page
    httpd_uri_t upload_page_uri = {
        .uri = "/upload",
        .method = HTTP_GET,
        .handler = upload_page_handler,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(server, &upload_page_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /upload");
        return ret;
    }

    // POST /api/files/upload
    httpd_uri_t upload_uri = {
        .uri = "/api/files/upload",
        .method = HTTP_POST,
        .handler = api_upload_handler,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(server, &upload_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/files/upload");
        return ret;
    }

    // GET /api/files/list
    httpd_uri_t list_uri = {
        .uri = "/api/files/list",
        .method = HTTP_GET,
        .handler = api_file_list_handler,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(server, &list_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/files/list");
        return ret;
    }

    // DELETE /api/files/delete
    httpd_uri_t delete_uri = {
        .uri = "/api/files/delete",
        .method = HTTP_DELETE,
        .handler = api_file_delete_handler,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(server, &delete_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/files/delete");
        return ret;
    }

    // GET /api/fs/info
    httpd_uri_t fs_info_uri = {
        .uri = "/api/fs/info",
        .method = HTTP_GET,
        .handler = api_fs_info_handler,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(server, &fs_info_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/fs/info");
        return ret;
    }

    // POST /api/fs/format
    httpd_uri_t format_uri = {
        .uri = "/api/fs/format",
        .method = HTTP_POST,
        .handler = api_fs_format_handler,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(server, &format_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/fs/format");
        return ret;
    }

    ESP_LOGI(TAG, "Upload handlers registered (6 endpoints)");
    return ESP_OK;
}
