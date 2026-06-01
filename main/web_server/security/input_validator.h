/**
 * @file input_validator.h
 * @brief 输入验证器
 *
 * 提供 URL 路径、文件名、请求内容的验证功能
 */

#ifndef INPUT_VALIDATOR_H
#define INPUT_VALIDATOR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ==================== 验证结果 ====================

typedef enum {
    VALIDATION_OK = 0,
    VALIDATION_ERROR_NULL,
    VALIDATION_ERROR_TOO_LONG,
    VALIDATION_ERROR_INVALID_CHARS,
    VALIDATION_ERROR_PATH_TRAVERSAL,
    VALIDATION_ERROR_FORBIDDEN_PATH,
    VALIDATION_ERROR_INVALID_EXTENSION,
    VALIDATION_ERROR_SIZE_EXCEEDED
} validation_result_t;

// ==================== 常量 ====================

#define MAX_PATH_LENGTH 256
#define MAX_FILENAME_LENGTH 128
#define MAX_EXTENSION_LENGTH 16

// ==================== API ====================

/**
 * @brief 验证 URL 路径
 * @param path URL 路径
 * @return validation_result_t 验证结果
 */
validation_result_t validate_url_path(const char *path);

/**
 * @brief 验证文件路径
 * @param filepath 完整文件路径
 * @return validation_result_t 验证结果
 */
validation_result_t validate_file_path(const char *filepath);

/**
 * @brief 验证文件名
 * @param filename 文件名
 * @return validation_result_t 验证结果
 */
validation_result_t validate_filename(const char *filename);

/**
 * @brief 验证文件扩展名是否在白名单中
 * @param filename 文件名
 * @param allowed_extensions 逗号分隔的扩展名列表，如 ".html,.css,.js"
 * @return true 允许
 * @return false 不允许
 */
bool validate_file_extension(const char *filename, const char *allowed_extensions);

/**
 * @brief 验证上传请求
 * @param filename 文件名
 * @param content_length 内容长度
 * @param max_size 最大允许大小
 * @return validation_result_t 验证结果
 */
validation_result_t validate_upload_request(const char *filename, size_t content_length, size_t max_size);

/**
 * @brief 验证 JSON 字符串
 * @param json JSON 字符串
 * @param max_len 最大长度
 * @return validation_result_t 验证结果
 */
validation_result_t validate_json_string(const char *json, size_t max_len);

/**
 * @brief 获取验证结果描述
 * @param result 验证结果
 * @return const char* 描述字符串
 */
const char* validation_result_str(validation_result_t result);

/**
 * @brief 检查字符是否为安全的 URL 字符
 */
static inline bool is_safe_url_char(char c)
{
    if (c >= 'a' && c <= 'z') return true;
    if (c >= 'A' && c <= 'Z') return true;
    if (c >= '0' && c <= '9') return true;
    if (c == '-' || c == '_' || c == '.' || c == '~') return true;
    if (c == '/' || c == '?' || c == '&' || c == '=') return true;
    return false;
}

/**
 * @brief 检查字符是否为安全的文件名字符
 */
static inline bool is_safe_filename_char(char c)
{
    if (c >= 'a' && c <= 'z') return true;
    if (c >= 'A' && c <= 'Z') return true;
    if (c >= '0' && c <= '9') return true;
    if (c == '-' || c == '_' || c == '.' || c == ' ') return true;
    return false;
}

#endif // INPUT_VALIDATOR_H
