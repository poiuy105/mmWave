#include "app_wdt.h"
#include "esp_task_wdt.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "APP_WDT";

// 每个任务的最后喂狗时间（tick count，避免溢出）
static volatile TickType_t s_last_feed_tick[WDT_TASK_COUNT];
// 任务是否已注册
static volatile bool s_registered[WDT_TASK_COUNT];
// 看门狗超时时间（秒）
static uint32_t s_timeout_sec;

// 任务 ID 到名称的映射（用于日志）
static const char *s_task_names[WDT_TASK_COUNT] = {
    [WDT_TASK_APP]            = "app",
    [WDT_TASK_DNS]            = "dns",
    [WDT_TASK_WS_HEARTBEAT]   = "ws_hb",
    [WDT_TASK_RADAR_BROADCAST] = "radar_bc",
    [WDT_TASK_RADAR_LD2450]   = "radar_ld2450",
    [WDT_TASK_RADAR_LD2452]   = "radar_ld2452",
    [WDT_TASK_RADAR_LD2460]   = "radar_ld2460",
    [WDT_TASK_RADAR_LD2461]   = "radar_ld2461",
    [WDT_TASK_RADAR_LD6002B]  = "radar_ld6002b",
    [WDT_TASK_RADAR_LD6004]   = "radar_ld6004",
    [WDT_TASK_RADAR_R60ABD1]  = "radar_r60abd1",
};

esp_err_t app_wdt_init(void)
{
    // 工业级：显式初始化所有状态，不依赖 BSS 段清零
    for (int i = 0; i < WDT_TASK_COUNT; i++) {
        s_last_feed_tick[i] = 0;
        s_registered[i] = false;
    }

    s_timeout_sec = CONFIG_ESP_TASK_WDT_TIMEOUT_S;

    ESP_LOGI(TAG, "Watchdog initialized, timeout=%lu sec", (unsigned long)s_timeout_sec);
    return ESP_OK;
}

esp_err_t app_wdt_register_task(wdt_task_id_t id)
{
    if (id >= WDT_TASK_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_registered[id]) {
        ESP_LOGW(TAG, "Task %s already registered", s_task_names[id]);
        return ESP_OK;
    }

    // 订阅当前任务到 TWDT
    esp_err_t ret = esp_task_wdt_add(NULL);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to subscribe task %s to TWDT: %s",
                 s_task_names[id], esp_err_to_name(ret));
        return ret;
    }
    if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGD(TAG, "Task %s already subscribed to TWDT (by ESP-IDF)", s_task_names[id]);
    }

    s_registered[id] = true;
    s_last_feed_tick[id] = xTaskGetTickCount();
    ESP_LOGI(TAG, "Task %s subscribed to TWDT", s_task_names[id]);
    return ESP_OK;
}

esp_err_t app_wdt_unregister_task(wdt_task_id_t id)
{
    if (id >= WDT_TASK_COUNT || !s_registered[id]) {
        return ESP_ERR_INVALID_ARG;
    }

    // 从 TWDT 取消订阅
    esp_err_t ret = esp_task_wdt_delete(NULL);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to unsubscribe task %s: %s",
                 s_task_names[id], esp_err_to_name(ret));
    }

    s_registered[id] = false;
    s_last_feed_tick[id] = 0;
    ESP_LOGI(TAG, "Task %s unsubscribed from TWDT", s_task_names[id]);
    return ret;
}

void app_wdt_feed(wdt_task_id_t id)
{
    if (id >= WDT_TASK_COUNT || !s_registered[id]) {
        return;
    }

    s_last_feed_tick[id] = xTaskGetTickCount();
    esp_task_wdt_reset();
}

bool app_wdt_is_healthy(wdt_task_id_t id)
{
    if (id >= WDT_TASK_COUNT) {
        return false;  // 无效 ID 视为不健康
    }
    if (!s_registered[id]) {
        return false;  // 未注册的任务视为不健康（工业级：明确状态）
    }

    TickType_t now = xTaskGetTickCount();
    TickType_t elapsed_ticks = now - s_last_feed_tick[id];
    TickType_t timeout_ticks = pdMS_TO_TICKS(s_timeout_sec * 1000);

    // 如果超过 1.5 倍超时时间未喂狗，视为不健康
    return (elapsed_ticks < (timeout_ticks * 3 / 2));
}

uint32_t app_wdt_get_last_feed_time(wdt_task_id_t id)
{
    if (id >= WDT_TASK_COUNT) {
        return 0;
    }
    // 返回秒数，使用 tick / configTICK_RATE_HZ 避免溢出
    return (uint32_t)(s_last_feed_tick[id] / configTICK_RATE_HZ);
}

uint32_t app_wdt_get_timeout_sec(void)
{
    return s_timeout_sec;
}
