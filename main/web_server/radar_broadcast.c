/**
 * @file radar_broadcast.c
 * @brief 雷达数据广播模块实现
 *
 * 定时任务：每 100ms (10Hz) 从 radar_adapter 获取数据，
 * 构建 JSON，通过 WebSocket 广播给所有客户端
 */

#include "radar_broadcast.h"
#include "app_wdt.h"
#include "config/server_config.h"
#include "radar_adapter/radar_adapter.h"
#include "websocket/ws_server.h"
#include "mqtt/app_mqtt.h"
#include "zone_detector/zone_detector.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "RADAR_BROADCAST";

static TaskHandle_t s_broadcast_task = NULL;
static volatile bool s_running = false;
static server_context_t *s_ctx = NULL;

/**
 * @brief 构建雷达数据 JSON
 *
 * 输出格式:
 * {"type":"radar_data","timestamp":123456,"frame_id":0,"target_count":1,
 *  "targets":[{"id":1,"x":1.5,"y":3.0,"z":0,"speed":0.5,"snr":30,"confidence":85}]}
 */
static char* build_radar_json(const radar_frame_t *frame, uint32_t frame_id)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddStringToObject(root, "type", "radar_data");
    cJSON_AddNumberToObject(root, "timestamp", esp_timer_get_time() / 1000);
    cJSON_AddNumberToObject(root, "frame_id", frame_id);
    cJSON_AddNumberToObject(root, "target_count", frame->target_count);

    cJSON *targets = cJSON_CreateArray();
    if (targets) {
        for (int i = 0; i < frame->target_count && i < 8; i++) {
            const radar_target_t *t = &frame->targets[i];
            if (!t->valid) continue;

            cJSON *item = cJSON_CreateObject();
            if (item) {
                cJSON_AddNumberToObject(item, "id", t->id);
                cJSON_AddNumberToObject(item, "x", t->x);
                cJSON_AddNumberToObject(item, "y", t->y);
                cJSON_AddNumberToObject(item, "z", t->z);
                cJSON_AddNumberToObject(item, "speed", t->speed);
                cJSON_AddNumberToObject(item, "snr", t->snr);
                cJSON_AddNumberToObject(item, "confidence", t->confidence);
                cJSON_AddItemToArray(targets, item);
            }
        }
        cJSON_AddItemToObject(root, "targets", targets);
    }

    // 添加区域检测状态
    cJSON *zones_json = zone_detector_get_status_json();
    if (zones_json) {
        cJSON_AddItemToObject(root, "zones", zones_json);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

/**
 * @brief 广播任务主循环
 */
static void radar_broadcast_task(void *arg)
{
    app_wdt_register_task(WDT_TASK_RADAR_BROADCAST);

    uint32_t frame_counter = 0;
    uint32_t broadcast_interval_ms = 500; // 2Hz - 降低 TCP 拥塞减少帧碎片概率
    uint32_t idle_interval_ms = 2000;    // 无目标时 0.5Hz

    // Use hardcoded interval to avoid uninitialized config
    ESP_LOGI(TAG, "Broadcast task started, interval=%lums", (unsigned long)broadcast_interval_ms);

    // 初始化区域检测器
    zone_detector_init();

    while (s_running) {
        radar_frame_t frame;

        // 从 radar_adapter 获取最新帧
        if (radar_adapter_get_frame(&frame) == ESP_OK) {
            // 处理区域检测
            zone_detector_process_frame(&frame);

            // 即使 target_count=0 也广播（让用户知道雷达工作正常）
            char *json = build_radar_json(&frame, frame_counter++);

            if (json) {
                // 广播给所有 WebSocket 客户端
                if (s_ctx && s_ctx->ws_server) {
                    int sent = ws_server_broadcast_text(s_ctx->ws_server, json);
                    if (sent > 0) {
                        ESP_LOGD(TAG, "Broadcast frame %lu to %d clients, targets=%d",
                                 (unsigned long)(frame_counter - 1), sent, frame.target_count);
                    }
                }
                
                // 同时发布到 MQTT（如果已连接）
                if (app_mqtt_is_connected()) {
                    app_mqtt_publish_radar_frame(&frame);
                }
                
                free(json);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(frame.target_count > 0 ? broadcast_interval_ms : idle_interval_ms));
        app_wdt_feed(WDT_TASK_RADAR_BROADCAST);
    }

    ESP_LOGI(TAG, "Broadcast task stopped");
    s_broadcast_task = NULL;
    app_wdt_unregister_task(WDT_TASK_RADAR_BROADCAST);
    vTaskDelete(NULL);
}

esp_err_t radar_broadcast_start(server_context_t *ctx)
{
    if (s_running) {
        ESP_LOGW(TAG, "Broadcast already running");
        return ESP_OK;
    }

    if (!ctx || !ctx->ws_server) {
        ESP_LOGE(TAG, "Invalid context or ws_server not ready");
        return ESP_ERR_INVALID_ARG;
    }

    s_ctx = ctx;
    s_running = true;

    // Use hardcoded values to avoid uninitialized config fields
    uint32_t stack_size = 16384;  // cJSON + zone_detector + MQTT publish 需要大量栈空间
    uint8_t priority = 5;  // Low priority, below HTTP server (12) and WiFi (23)

    BaseType_t ret = xTaskCreate(
        radar_broadcast_task,
        "radar_bc",
        stack_size,
        NULL,
        priority,
        &s_broadcast_task
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create broadcast task");
        s_running = false;
        s_ctx = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Broadcast started");
    return ESP_OK;
}

esp_err_t radar_broadcast_stop(void)
{
    if (!s_running) {
        return ESP_OK;
    }

    s_running = false;

    // 等待任务结束（给一个广播周期 + 余量）
    if (s_broadcast_task) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    s_ctx = NULL;
    ESP_LOGI(TAG, "Broadcast stopped");
    return ESP_OK;
}

bool radar_broadcast_is_running(void)
{
    return s_running;
}
