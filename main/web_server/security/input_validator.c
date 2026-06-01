/**
 * @file input_validator.c
 * @brief Input validator implementation
 */

#include "input_validator.h"
#include "esp_log.h"
#include <string.h>
#include <ctype.h>

static const char *TAG = "INPUT_VALIDATOR";

validation_result_t validate_url_path(const char *path)
{
    if (path == NULL) {
        return VALIDATION_ERROR_NULL;
    }

    size_t len = strlen(path);

    if (len == 0 || len > MAX_PATH_LENGTH) {
        return VALIDATION_ERROR_TOO_LONG;
    }

    if (path[0] != '/') {
        return VALIDATION_ERROR_INVALID_CHARS;
    }

    for (size_t i = 0; i < len; i++) {
        if (!is_safe_url_char(path[i])) {
            ESP_LOGW(TAG, "Invalid URL char at pos %zu: 0x%02X", i, (unsigned char)path[i]);
            return VALIDATION_ERROR_INVALID_CHARS;
        }
    }

    if (strstr(path, "..") != NULL) {
        ESP_LOGW(TAG, "Path traversal detected: %s", path);
        return VALIDATION_ERROR_PATH_TRAVERSAL;
    }

    return VALIDATION_OK;
}

validation_result_t validate_file_path(const char *filepath)
{
    if (filepath == NULL) {
        return VALIDATION_ERROR_NULL;
    }

    size_t len = strlen(filepath);

    if (len == 0 || len > MAX_PATH_LENGTH) {
        return VALIDATION_ERROR_TOO_LONG;
    }

    for (size_t i = 0; i < len; i++) {
        char c = filepath[i];
        if (c < 32 || c == '\\' || c == '"' || c == '\'' || c == ';' || c == '`') {
            ESP_LOGW(TAG, "Invalid filepath char at pos %zu: 0x%02X", i, (unsigned char)c);
            return VALIDATION_ERROR_INVALID_CHARS;
        }
    }

    if (strstr(filepath, "..") != NULL) {
        ESP_LOGW(TAG, "Path traversal in filepath: %s", filepath);
        return VALIDATION_ERROR_PATH_TRAVERSAL;
    }

    return VALIDATION_OK;
}

validation_result_t validate_filename(const char *filename)
{
    if (filename == NULL) {
        return VALIDATION_ERROR_NULL;
    }

    size_t len = strlen(filename);

    if (len == 0 || len > MAX_FILENAME_LENGTH) {
        return VALIDATION_ERROR_TOO_LONG;
    }

    for (size_t i = 0; i < len; i++) {
        if (!is_safe_filename_char(filename[i])) {
            ESP_LOGW(TAG, "Invalid filename char at pos %zu: '%c' (0x%02X)",
                     i, filename[i], (unsigned char)filename[i]);
            return VALIDATION_ERROR_INVALID_CHARS;
        }
    }

    if (filename[0] == '.') {
        return VALIDATION_ERROR_FORBIDDEN_PATH;
    }

    return VALIDATION_OK;
}

bool validate_file_extension(const char *filename, const char *allowed_extensions)
{
    if (filename == NULL || allowed_extensions == NULL) {
        return false;
    }

    const char *ext = strrchr(filename, '.');
    if (ext == NULL) {
        return strstr(allowed_extensions, ",") != NULL;
    }

    const char *p = allowed_extensions;
    size_t ext_len = strlen(ext);

    while (*p) {
        while (*p == ',') p++;
        if (*p == '\0') break;

        size_t allowed_len = 0;
        while (p[allowed_len] && p[allowed_len] != ',') {
            allowed_len++;
        }

        if (allowed_len == ext_len && strncmp(ext, p, ext_len) == 0) {
            return true;
        }

        p += allowed_len;
    }

    ESP_LOGW(TAG, "Extension '%s' not in whitelist: %s", ext, allowed_extensions);
    return false;
}

validation_result_t validate_upload_request(const char *filename, size_t content_length, size_t max_size)
{
    validation_result_t result = validate_filename(filename);
    if (result != VALIDATION_OK) {
        return result;
    }

    if (content_length > max_size) {
        ESP_LOGW(TAG, "Upload size %zu exceeds max %zu", content_length, max_size);
        return VALIDATION_ERROR_SIZE_EXCEEDED;
    }

    return VALIDATION_OK;
}

validation_result_t validate_json_string(const char *json, size_t max_len)
{
    if (json == NULL) {
        return VALIDATION_ERROR_NULL;
    }

    size_t len = strlen(json);

    if (len == 0) {
        return VALIDATION_ERROR_INVALID_CHARS;
    }

    if (len > max_len) {
        return VALIDATION_ERROR_TOO_LONG;
    }

    char c = json[0];
    if (c != '{' && c != '[') {
        return VALIDATION_ERROR_INVALID_CHARS;
    }

    int brace_count = 0;
    int bracket_count = 0;
    bool in_string = false;
    char prev = 0;

    for (size_t i = 0; i < len; i++) {
        c = json[i];

        if (c == '"' && prev != '\\') {
            in_string = !in_string;
        }

        if (!in_string) {
            if (c == '{') brace_count++;
            if (c == '}') brace_count--;
            if (c == '[') bracket_count++;
            if (c == ']') bracket_count--;
        }

        prev = c;

        if (brace_count < 0 || bracket_count < 0) {
            return VALIDATION_ERROR_INVALID_CHARS;
        }
    }

    if (brace_count != 0 || bracket_count != 0) {
        return VALIDATION_ERROR_INVALID_CHARS;
    }

    return VALIDATION_OK;
}

const char* validation_result_str(validation_result_t result)
{
    switch (result) {
        case VALIDATION_OK:
            return "OK";
        case VALIDATION_ERROR_NULL:
            return "Null input";
        case VALIDATION_ERROR_TOO_LONG:
            return "Input too long";
        case VALIDATION_ERROR_INVALID_CHARS:
            return "Invalid characters";
        case VALIDATION_ERROR_PATH_TRAVERSAL:
            return "Path traversal detected";
        case VALIDATION_ERROR_FORBIDDEN_PATH:
            return "Forbidden path";
        case VALIDATION_ERROR_INVALID_EXTENSION:
            return "Invalid file extension";
        case VALIDATION_ERROR_SIZE_EXCEEDED:
            return "Size exceeded";
        default:
            return "Unknown error";
    }
}
