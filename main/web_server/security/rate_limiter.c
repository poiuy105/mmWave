/**
 * @file rate_limiter.c
 * @brief 速率限制器实现
 */

#include "rate_limiter.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "RATE_LIMITER";

#define MAX_IP_LENGTH 16

typedef struct {
    char ip[MAX_IP_LENGTH];
    uint32_t request_count;
    uint32_t window_start;      // 系统启动后的毫秒数
    bool blocked;
    uint32_t block_until;       // 阻塞截止时间
    bool used;                   // 条目是否在使用
} rate_limit_entry_t;

static rate_limit_entry_t *s_entries = NULL;
static uint16_t s_max_entries = 32;
static uint16_t s_max_requests = 20;
static uint16_t s_window_ms = 1000;
static uint16_t s_block_duration_sec = 5;
static SemaphoreHandle_t s_mutex = NULL;

static uint32_t s_total_hits = 0;
static uint32_t s_blocked_hits = 0;

static const rate_limiter_config_t s_default_config = {
    .max_requests = 20,
    .window_ms = 1000,
    .block_duration_sec = 5,
    .max_entries = 32
};

static uint32_t get_time_ms(void)
{
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

static rate_limit_entry_t* find_or_create_entry(const char *ip)
{
    rate_limit_entry_t *free_entry = NULL;

    for (uint16_t i = 0; i < s_max_entries; i++) {
        if (s_entries[i].used && strcmp(s_entries[i].ip, ip) == 0) {
            return &s_entries[i];
        }
        if (!free_entry && !s_entries[i].used) {
            free_entry = &s_entries[i];
        }
    }

    return free_entry;
}

esp_err_t rate_limiter_init(const rate_limiter_config_t *config)
{
    if (config == NULL) {
        config = &s_default_config;
    }

    if (s_mutex != NULL) {
        ESP_LOGW(TAG, "Rate limiter already initialized");
        return ESP_OK;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    s_max_requests = config->max_requests;
    s_window_ms = config->window_ms;
    s_block_duration_sec = config->block_duration_sec;
    s_max_entries = config->max_entries;

    s_entries = calloc(s_max_entries, sizeof(rate_limit_entry_t));
    if (s_entries == NULL) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        ESP_LOGE(TAG, "Failed to allocate entries");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Rate limiter initialized: max_req=%d, window=%dms, block=%ds, entries=%d",
             s_max_requests, s_window_ms, s_block_duration_sec, s_max_entries);

    return ESP_OK;
}

void rate_limiter_deinit(void)
{
    if (s_mutex != NULL) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }

    if (s_entries != NULL) {
        free(s_entries);
        s_entries = NULL;
    }

    s_total_hits = 0;
    s_blocked_hits = 0;

    ESP_LOGI(TAG, "Rate limiter deinitialized");
}

bool rate_limiter_check(const char *client_ip)
{
    if (client_ip == NULL || s_mutex == NULL || s_entries == NULL) {
        return true;  // 默认允许
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    s_total_hits++;

    rate_limit_entry_t *entry = find_or_create_entry(client_ip);

    if (entry == NULL) {
        // 没有空闲条目，拒绝
        s_blocked_hits++;
        xSemaphoreGive(s_mutex);
        ESP_LOGW(TAG, "Rate limit table full, rejecting %s", client_ip);
        return false;
    }

    uint32_t now = get_time_ms();

    // 初始化新条目
    if (!entry->used) {
        memset(entry, 0, sizeof(rate_limit_entry_t));
        strncpy(entry->ip, client_ip, MAX_IP_LENGTH - 1);
        entry->used = true;
        entry->window_start = now;
        entry->request_count = 1;
        xSemaphoreGive(s_mutex);
        return true;
    }

    // 检查是否被阻塞
    if (entry->blocked) {
        if (now < entry->block_until) {
            s_blocked_hits++;
            xSemaphoreGive(s_mutex);
            return false;  // 仍在阻塞期
        }
        // 阻塞期已过，重置
        entry->blocked = false;
        entry->request_count = 0;
        entry->window_start = now;
    }

    // 检查时间窗口
    uint32_t elapsed = now - entry->window_start;
    if (elapsed > s_window_ms) {
        // 新窗口
        entry->request_count = 1;
        entry->window_start = now;
        xSemaphoreGive(s_mutex);
        return true;
    }

    // 窗口内计数
    entry->request_count++;

    if (entry->request_count > s_max_requests) {
        // 超限，阻塞
        entry->blocked = true;
        entry->block_until = now + (s_block_duration_sec * 1000);
        s_blocked_hits++;
        xSemaphoreGive(s_mutex);
        ESP_LOGW(TAG, "Rate limit exceeded for %s: %d req in %dms",
                 client_ip, entry->request_count, elapsed);
        return false;
    }

    xSemaphoreGive(s_mutex);
    return true;
}

void rate_limiter_reset(const char *client_ip)
{
    if (client_ip == NULL || s_mutex == NULL || s_entries == NULL) {
        return;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    rate_limit_entry_t *entry = find_or_create_entry(client_ip);
    if (entry && entry->used) {
        entry->request_count = 0;
        entry->window_start = get_time_ms();
        entry->blocked = false;
    }

    xSemaphoreGive(s_mutex);
}

void rate_limiter_get_stats(uint32_t *total_hits, uint32_t *blocked_hits, uint16_t *active_entries)
{
    if (s_mutex != NULL) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    }

    if (total_hits) *total_hits = s_total_hits;
    if (blocked_hits) *blocked_hits = s_blocked_hits;

    if (active_entries && s_entries) {
        uint16_t count = 0;
        for (uint16_t i = 0; i < s_max_entries; i++) {
            if (s_entries[i].used) count++;
        }
        *active_entries = count;
    }

    if (s_mutex != NULL) {
        xSemaphoreGive(s_mutex);
    }
}

bool rate_limiter_is_initialized(void)
{
    return s_mutex != NULL;
}

const rate_limiter_config_t* rate_limiter_get_default_config(void)
{
    return &s_default_config;
}
