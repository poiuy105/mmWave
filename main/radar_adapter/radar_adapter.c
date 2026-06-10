/**
 * @file radar_adapter.c
 * @brief 雷达适配层实现
 */

#include "radar_adapter.h"
#include "radar.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

#define FRAME_MUTEX_TIMEOUT_MS  10  /*!< 帧互斥锁超时 10ms */

static const char *TAG = "RADAR_ADAPT";

// 雷达句柄
static radar_handle_t s_radar = NULL;

// 数据缓存（线程安全）
static radar_frame_t s_latest_frame;
static SemaphoreHandle_t s_frame_mutex = NULL;

// 状态
static radar_status_t s_status = {0};
static uint32_t s_frame_counter = 0;

/* ============ 雷达信息（编译时确定） ============ */

// 编译时验证：确保只有一个雷达类型被定义
#if defined(CONFIG_RADAR_LD2460) && defined(CONFIG_RADAR_LD2452)
#error "Both CONFIG_RADAR_LD2460 and CONFIG_RADAR_LD2452 are defined!"
#endif

#if !defined(CONFIG_RADAR_LD2452)
#error "CONFIG_RADAR_LD2452 is not defined!"
#endif

#if defined(CONFIG_RADAR_LD2460)
static const radar_info_t s_radar_info = {
    .type = "LD2460",
    .name = "HLK-LD2460 (2T4R Multi-target Tracking)",
    .max_targets = 8,
    .has_3d = false,
    .has_install_mode = true,
    .has_region_filter = false,
    .has_sensitivity = true,
    .has_sleep_monitoring = false,
    .default_baud_rate = 115200,
};

#elif defined(CONFIG_RADAR_LD2450)
static const radar_info_t s_radar_info = {
    .type = "LD2450",
    .name = "HLK-LD2450 (1T2R Multi-target Tracking)",
    .max_targets = 3,
    .has_3d = false,
    .has_install_mode = false,
    .has_region_filter = true,
    .has_sensitivity = false,
    .has_sleep_monitoring = false,
    .default_baud_rate = 256000,
};

#elif defined(CONFIG_RADAR_LD2452)
static const radar_info_t s_radar_info = {
    .type = "LD2452",
    .name = "HLK-LD2452 (1T2R Multi-target Tracking)",
    .max_targets = 3,
    .has_3d = false,
    .has_install_mode = false,
    .has_region_filter = false,
    .has_sensitivity = false,
    .has_sleep_monitoring = false,
    .default_baud_rate = 9600,
};

#elif defined(CONFIG_RADAR_LD2461)
static const radar_info_t s_radar_info = {
    .type = "LD2461",
    .name = "HLK-LD2461 (2T4R Motion Detection)",
    .max_targets = 1,  // 区域状态，非目标跟踪
    .has_3d = false,
    .has_install_mode = false,
    .has_region_filter = true,
    .has_sensitivity = false,
    .has_sleep_monitoring = false,
    .default_baud_rate = 9600,
};

#elif defined(CONFIG_RADAR_LD6002B)
static const radar_info_t s_radar_info = {
    .type = "LD6002B",
    .name = "HLK-LD6002B (3D Presence Detection)",
    .max_targets = 1,
    .has_3d = true,
    .has_install_mode = false,
    .has_region_filter = true,
    .has_sensitivity = false,
    .has_sleep_monitoring = false,
    .default_baud_rate = 115200,
};

#elif defined(CONFIG_RADAR_LD6004)
static const radar_info_t s_radar_info = {
    .type = "LD6004",
    .name = "HLK-LD6004 (3D Presence Detection)",
    .max_targets = 1,
    .has_3d = true,
    .has_install_mode = false,
    .has_region_filter = true,
    .has_sensitivity = false,
    .has_sleep_monitoring = false,
    .default_baud_rate = 115200,
};

#elif defined(CONFIG_RADAR_R60ABD1)
static const radar_info_t s_radar_info = {
    .type = "R60ABD1",
    .name = "R60ABD1 (60GHz Sleep/Breath/Heart Rate Monitor)",
    .max_targets = 1,
    .has_3d = false,
    .has_install_mode = false,
    .has_region_filter = false,
    .has_sensitivity = false,
    .has_sleep_monitoring = true,
    .default_baud_rate = 115200,
};

#else
static const radar_info_t s_radar_info = {
    .type = "Unknown",
    .name = "Unknown Radar",
    .max_targets = 0,
    .has_3d = false,
    .has_install_mode = false,
    .has_region_filter = false,
    .has_sensitivity = false,
    .has_sleep_monitoring = false,
    .default_baud_rate = 115200,
};
#endif

/* ============ 数据转换函数 ============ */

#if defined(CONFIG_RADAR_LD2460)
/**
 * @brief 转换 LD2460 数据为统一格式
 * 
 * LD2460 原始数据：x, y 为 int16_t，单位 0.1m
 * 转换后：float，单位 m
 */
static void convert_ld2460_data(const ld2460_data_t *src, radar_frame_t *dst)
{
    dst->frame_id = ++s_frame_counter;
    dst->timestamp_ms = esp_timer_get_time() / 1000;
    dst->target_count = src->target_count;
    dst->has_data = true;
    
    for (int i = 0; i < src->target_count && i < RADAR_MAX_TARGETS; i++) {
        dst->targets[i].id = i + 1;
        dst->targets[i].x = src->targets[i].x * 0.1f;  // 0.1m → m
        dst->targets[i].y = src->targets[i].y * 0.1f;  // 0.1m → m
        dst->targets[i].z = 0.0f;  // 2D 雷达
        dst->targets[i].speed = 0.0f;  // LD2460 不提供速度
        dst->targets[i].snr = 0.0f;
        dst->targets[i].confidence = 100;
        dst->targets[i].valid = true;
    }
    
    // 清空未使用的目标槽位
    for (int i = src->target_count; i < RADAR_MAX_TARGETS; i++) {
        memset(&dst->targets[i], 0, sizeof(radar_target_t));
        dst->targets[i].valid = false;
    }
}
#endif

#if defined(CONFIG_RADAR_LD2450)
/**
 * @brief 转换 LD2450 数据为统一格式
 * 
 * LD2450 原始数据：x, y 为 int16_t，单位 mm
 * 转换后：float，单位 m
 */
static void convert_ld2450_data(const ld2450_data_t *src, radar_frame_t *dst)
{
    dst->frame_id = ++s_frame_counter;
    dst->timestamp_ms = esp_timer_get_time() / 1000;
    dst->target_count = src->target_count;
    dst->has_data = true;
    
    for (int i = 0; i < src->target_count && i < RADAR_MAX_TARGETS; i++) {
        dst->targets[i].id = i + 1;
        dst->targets[i].x = src->targets[i].x * 0.001f;  // mm → m
        dst->targets[i].y = src->targets[i].y * 0.001f;  // mm → m
        dst->targets[i].z = 0.0f;  // 2D 雷达
        dst->targets[i].speed = src->targets[i].speed * 0.01f;  // cm/s → m/s
        dst->targets[i].snr = 0.0f;
        dst->targets[i].confidence = 100;
        dst->targets[i].valid = true;
    }
    
    for (int i = src->target_count; i < RADAR_MAX_TARGETS; i++) {
        memset(&dst->targets[i], 0, sizeof(radar_target_t));
        dst->targets[i].valid = false;
    }
}
#endif

#if defined(CONFIG_RADAR_LD2452)
/**
 * @brief 转换 LD2452 数据为统一格式
 */
static void convert_ld2452_data(const ld2452_data_t *src, radar_frame_t *dst)
{
    dst->frame_id = ++s_frame_counter;
    dst->timestamp_ms = esp_timer_get_time() / 1000;
    dst->target_count = src->target_count;
    dst->has_data = true;
    
    for (int i = 0; i < src->target_count && i < RADAR_MAX_TARGETS; i++) {
        dst->targets[i].id = i + 1;
        dst->targets[i].x = src->targets[i].x * 0.001f;  // mm → m
        dst->targets[i].y = src->targets[i].y * 0.001f;  // mm → m
        dst->targets[i].z = 0.0f;
        dst->targets[i].speed = src->targets[i].speed * 0.01f;  // cm/s → m/s
        dst->targets[i].snr = 0.0f;
        dst->targets[i].confidence = 100;
        dst->targets[i].valid = true;
    }
    
    for (int i = src->target_count; i < RADAR_MAX_TARGETS; i++) {
        memset(&dst->targets[i], 0, sizeof(radar_target_t));
        dst->targets[i].valid = false;
    }
}
#endif

#if defined(CONFIG_RADAR_LD6002B)
/**
 * @brief 转换 LD6002B 数据为统一格式
 */
static void convert_ld6002b_data(const ld6002b_data_t *src, radar_frame_t *dst)
{
    dst->frame_id = ++s_frame_counter;
    dst->timestamp_ms = esp_timer_get_time() / 1000;
    dst->target_count = 1;
    dst->has_data = true;
    
    dst->targets[0].id = 1;
    dst->targets[0].x = src->x;
    dst->targets[0].y = src->y;
    dst->targets[0].z = src->z;
    dst->targets[0].speed = 0.0f;
    dst->targets[0].snr = 0.0f;
    dst->targets[0].confidence = 100;
    dst->targets[0].valid = true;
    
    for (int i = 1; i < RADAR_MAX_TARGETS; i++) {
        memset(&dst->targets[i], 0, sizeof(radar_target_t));
        dst->targets[i].valid = false;
    }
}
#endif

#if defined(CONFIG_RADAR_LD6004)
/**
 * @brief 转换 LD6004 数据为统一格式
 */
static void convert_ld6004_data(const ld6004_data_t *src, radar_frame_t *dst)
{
    dst->frame_id = ++s_frame_counter;
    dst->timestamp_ms = esp_timer_get_time() / 1000;
    dst->target_count = 1;
    dst->has_data = true;
    
    dst->targets[0].id = 1;
    dst->targets[0].x = src->x;
    dst->targets[0].y = src->y;
    dst->targets[0].z = src->z;
    dst->targets[0].speed = 0.0f;
    dst->targets[0].snr = 0.0f;
    dst->targets[0].confidence = 100;
    dst->targets[0].valid = true;
    
    for (int i = 1; i < RADAR_MAX_TARGETS; i++) {
        memset(&dst->targets[i], 0, sizeof(radar_target_t));
        dst->targets[i].valid = false;
    }
}
#endif

/* ============ 事件处理函数 ============ */

/**
 * @brief 雷达事件处理函数
 */
static void radar_event_handler(void *handler_args, esp_event_base_t base,
                                 int32_t event_id, void *event_data)
{
    if (event_id != RADAR_EVENT_TARGET) {
        return;
    }
    
    radar_data_t *data = (radar_data_t *)event_data;
    
    // 获取互斥锁
    if (xSemaphoreTake(s_frame_mutex, pdMS_TO_TICKS(FRAME_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to take mutex, frame dropped");
        s_status.error_count++;
        return;
    }
    
    // 根据雷达类型转换数据
#if defined(CONFIG_RADAR_LD2460)
    convert_ld2460_data((ld2460_data_t *)data, &s_latest_frame);
#elif defined(CONFIG_RADAR_LD2450)
    convert_ld2450_data((ld2450_data_t *)data, &s_latest_frame);
#elif defined(CONFIG_RADAR_LD2452)
    convert_ld2452_data((ld2452_data_t *)data, &s_latest_frame);
#elif defined(CONFIG_RADAR_LD6002B)
    convert_ld6002b_data((ld6002b_data_t *)data, &s_latest_frame);
#elif defined(CONFIG_RADAR_LD6004)
    convert_ld6004_data((ld6004_data_t *)data, &s_latest_frame);
#else
    ESP_LOGW(TAG, "Unknown radar type, cannot convert data");
#endif
    
    // 更新状态
    s_status.frame_count++;
    s_status.last_update_ms = esp_timer_get_time() / 1000;
    
    uint32_t frame_id = s_frame_counter;
    int target_count = s_latest_frame.target_count;
    
    xSemaphoreGive(s_frame_mutex);
    
    ESP_LOGD(TAG, "Frame %lu: %d targets", (unsigned long)frame_id, target_count);
}

/* ============ 公共 API 实现 ============ */

esp_err_t radar_adapter_init(void)
{
    if (s_radar != NULL) {
        ESP_LOGW(TAG, "Radar adapter already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing radar adapter for %s...", s_radar_info.type);
    
    // 创建互斥锁
    s_frame_mutex = xSemaphoreCreateMutex();
    if (s_frame_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // 初始化帧缓存
    memset(&s_latest_frame, 0, sizeof(radar_frame_t));
    memset(&s_status, 0, sizeof(radar_status_t));
    s_frame_counter = 0;
    
    // 初始化雷达驱动
    radar_config_t config = RADAR_CONFIG_DEFAULT();
    s_radar = RADAR_INIT(&config);
    if (s_radar == NULL) {
        ESP_LOGE(TAG, "Failed to initialize radar driver");
        vSemaphoreDelete(s_frame_mutex);
        s_frame_mutex = NULL;
        return ESP_FAIL;
    }
    
    // 注册事件处理
    esp_err_t err = RADAR_ADD_HANDLER(s_radar, radar_event_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add event handler: %s", esp_err_to_name(err));
        RADAR_DEINIT(s_radar);
        s_radar = NULL;
        vSemaphoreDelete(s_frame_mutex);
        s_frame_mutex = NULL;
        return err;
    }
    
    s_status.initialized = true;
    s_status.report_enabled = true;
    
    ESP_LOGI(TAG, "Radar adapter initialized successfully");
    ESP_LOGI(TAG, "  Type: %s", s_radar_info.type);
    ESP_LOGI(TAG, "  Max targets: %d", s_radar_info.max_targets);
    ESP_LOGI(TAG, "  3D support: %s", s_radar_info.has_3d ? "Yes" : "No");
    ESP_LOGI(TAG, "  Install mode: %s", s_radar_info.has_install_mode ? "Yes" : "No");
    
    return ESP_OK;
}

esp_err_t radar_adapter_deinit(void)
{
    if (s_radar == NULL) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Deinitializing radar adapter...");
    
    RADAR_RM_HANDLER(s_radar, radar_event_handler);
    RADAR_DEINIT(s_radar);
    s_radar = NULL;
    
    if (s_frame_mutex) {
        vSemaphoreDelete(s_frame_mutex);
        s_frame_mutex = NULL;
    }
    
    s_status.initialized = false;
    
    return ESP_OK;
}

bool radar_adapter_is_initialized(void)
{
    return s_status.initialized;
}

esp_err_t radar_adapter_get_frame(radar_frame_t *frame)
{
    if (frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_status.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(s_frame_mutex, pdMS_TO_TICKS(FRAME_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    if (!s_latest_frame.has_data) {
        xSemaphoreGive(s_frame_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    
    memcpy(frame, &s_latest_frame, sizeof(radar_frame_t));
    xSemaphoreGive(s_frame_mutex);
    
    return ESP_OK;
}

esp_err_t radar_adapter_get_info(radar_info_t *info)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(info, &s_radar_info, sizeof(radar_info_t));
    return ESP_OK;
}

esp_err_t radar_adapter_get_status(radar_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(status, &s_status, sizeof(radar_status_t));
    return ESP_OK;
}

esp_err_t radar_adapter_enable_report(bool enable)
{
    if (!s_status.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
#if defined(CONFIG_RADAR_LD2460)
    esp_err_t err = ld2460_enable_report(s_radar, enable);
#elif defined(CONFIG_RADAR_LD2450)
    esp_err_t err = ld2450_enable_report(s_radar, enable);
#else
    // 其他雷达可能不支持此功能
    esp_err_t err = ESP_OK;
#endif
    
    if (err == ESP_OK) {
        s_status.report_enabled = enable;
        ESP_LOGI(TAG, "Report %s", enable ? "enabled" : "disabled");
    }
    
    return err;
}

esp_err_t radar_adapter_restart(void)
{
    if (!s_status.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
#if defined(CONFIG_RADAR_LD2460)
    return ld2460_restart(s_radar);
#elif defined(CONFIG_RADAR_LD2450)
    return ld2450_restart(s_radar);
#elif defined(CONFIG_RADAR_LD6004)
    return ld6004_restart(s_radar);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t radar_adapter_factory_reset(void)
{
    if (!s_status.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
#if defined(CONFIG_RADAR_LD2460)
    return ld2460_factory_reset(s_radar);
#elif defined(CONFIG_RADAR_LD2450)
    return ld2450_factory_reset(s_radar);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

uint32_t radar_adapter_get_capabilities(void)
{
    uint32_t caps = 0;
    
    if (s_radar_info.has_3d) caps |= RADAR_CAP_3D;
    if (s_radar_info.has_install_mode) caps |= RADAR_CAP_INSTALL_MODE;
    if (s_radar_info.has_region_filter) caps |= RADAR_CAP_REGION_FILTER;
    if (s_radar_info.has_sensitivity) caps |= RADAR_CAP_SENSITIVITY;
    if (s_radar_info.has_sleep_monitoring) caps |= RADAR_CAP_SLEEP_MONITOR;
    
    // 大多数雷达支持检测范围设置
#if defined(CONFIG_RADAR_LD2460) || defined(CONFIG_RADAR_LD2450) || \
    defined(CONFIG_RADAR_LD6002B) || defined(CONFIG_RADAR_LD6004)
    caps |= RADAR_CAP_DETECTION_RANGE;
#endif
    
    return caps;
}

void* radar_adapter_get_handle(void)
{
    return s_radar;
}
