/**
 * @file radar_adapter.h
 * @brief 雷达适配层 - 统一数据格式和接口
 * 
 * 封装 radar.h 统一接口，提供：
 * - 数据缓存（最新帧数据）
 * - 格式转换（原始数据 → 统一格式）
 * - 状态管理
 */

#ifndef RADAR_ADAPTER_H
#define RADAR_ADAPTER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============ 统一数据结构 ============ */

/**
 * @brief 统一的目标数据结构
 * 
 * 所有雷达的目标数据都转换为此格式
 * 坐标系：以雷达为原点，正前方为 Y 轴正方向
 */
typedef struct {
    uint8_t id;           /*!< 目标 ID (1-based) */
    float x;              /*!< X 坐标 (米)，左侧为负，右侧为正 */
    float y;              /*!< Y 坐标 (米)，正前方为正 */
    float z;              /*!< Z 坐标 (米)，2D 雷达为 0 */
    float speed;          /*!< 速度 (m/s)，有符号 */
    float snr;            /*!< 信噪比 (dB)，部分雷达支持 */
    uint8_t confidence;   /*!< 置信度 0-100，部分雷达支持 */
    bool valid;           /*!< 目标是否有效 */
} radar_target_t;

/**
 * @brief 统一的雷达帧数据
 */
typedef struct {
    uint32_t frame_id;                    /*!< 帧序号 */
    uint64_t timestamp_ms;                /*!< 时间戳 (毫秒) */
    uint8_t target_count;                 /*!< 有效目标数量 */
    radar_target_t targets[8];            /*!< 目标数组 */
    bool has_data;                        /*!< 是否有数据 */
} radar_frame_t;

/**
 * @brief 雷达信息结构
 */
typedef struct {
    const char *type;           /*!< 雷达型号字符串，如 "LD2460" */
    const char *name;           /*!< 雷达名称，如 "HLK-LD2460 (2T4R)" */
    uint8_t max_targets;        /*!< 最大目标数 */
    bool has_3d;                /*!< 是否支持 3D 坐标 */
    bool has_install_mode;      /*!< 是否支持安装模式设置 */
    bool has_region_filter;     /*!< 是否支持区域过滤 */
    bool has_sensitivity;       /*!< 是否支持灵敏度设置 */
    bool has_sleep_monitoring;  /*!< 是否支持睡眠监测 */
    uint32_t default_baud_rate; /*!< 默认波特率 */
} radar_info_t;

/**
 * @brief 雷达状态
 */
typedef struct {
    bool initialized;          /*!< 是否已初始化 */
    bool report_enabled;       /*!< 数据上报是否启用 */
    uint32_t frame_count;      /*!< 累计帧数 */
    uint32_t error_count;      /*!< 错误计数 */
    uint64_t last_update_ms;   /*!< 最后更新时间 */
} radar_status_t;

/* ============ 初始化 API ============ */

/**
 * @brief 初始化雷达适配层
 * 
 * 初始化雷达驱动，注册事件处理
 * 
 * @return ESP_OK 成功
 */
esp_err_t radar_adapter_init(void);

/**
 * @brief 反初始化雷达适配层
 * 
 * @return ESP_OK 成功
 */
esp_err_t radar_adapter_deinit(void);

/**
 * @brief 检查雷达是否已初始化
 * 
 * @return true 已初始化
 */
bool radar_adapter_is_initialized(void);

/* ============ 数据获取 API ============ */

/**
 * @brief 获取最新帧数据（线程安全）
 * 
 * @param frame 输出帧数据
 * @return ESP_OK 成功，ESP_ERR_NOT_FOUND 无数据
 */
esp_err_t radar_adapter_get_frame(radar_frame_t *frame);

/**
 * @brief 获取雷达信息
 * 
 * @param info 输出雷达信息
 * @return ESP_OK 成功
 */
esp_err_t radar_adapter_get_info(radar_info_t *info);

/**
 * @brief 获取雷达状态
 * 
 * @param status 输出状态
 * @return ESP_OK 成功
 */
esp_err_t radar_adapter_get_status(radar_status_t *status);

/* ============ 控制 API ============ */

/**
 * @brief 启用/禁用雷达数据上报
 * 
 * @param enable true 启用，false 禁用
 * @return ESP_OK 成功
 */
esp_err_t radar_adapter_enable_report(bool enable);

/**
 * @brief 重启雷达模块
 * 
 * @return ESP_OK 成功
 */
esp_err_t radar_adapter_restart(void);

/**
 * @brief 恢复出厂设置
 * 
 * @return ESP_OK 成功
 */
esp_err_t radar_adapter_factory_reset(void);

/* ============ 雷达特有功能 API ============ */

/**
 * @brief 获取雷达特有功能的能力掩码
 * 
 * @return 能力位掩码
 */
uint32_t radar_adapter_get_capabilities(void);

/**
 * @brief 获取雷达驱动句柄（用于特有功能 API）
 * 
 * @return 雷达句柄，未初始化时返回 NULL
 */
void* radar_adapter_get_handle(void);

/* 能力位定义 */
#define RADAR_CAP_3D              BIT(0)  /*!< 支持 3D 坐标 */
#define RADAR_CAP_INSTALL_MODE    BIT(1)  /*!< 支持安装模式 */
#define RADAR_CAP_REGION_FILTER   BIT(2)  /*!< 支持区域过滤 */
#define RADAR_CAP_SENSITIVITY     BIT(3)  /*!< 支持灵敏度设置 */
#define RADAR_CAP_SLEEP_MONITOR   BIT(4)  /*!< 支持睡眠监测 */
#define RADAR_CAP_DETECTION_RANGE BIT(5)  /*!< 支持检测范围设置 */

#ifdef __cplusplus
}
#endif

#endif // RADAR_ADAPTER_H
