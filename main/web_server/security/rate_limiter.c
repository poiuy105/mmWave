/**
 * @file rate_limiter.c
 * @brief Rate limiter implementation
 */

#include "rate_limiter.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "RATE_LIMITER";

#define MAX_IP_LENGTH 16

typedef struct {
    char ip[MAX_IP_LENGTH];
    uint32_t request_count;
    int64_t window_start;       // Use 64-bit microsecond timestamp (Fix #18)
    bool blocked;
    int64_t block_until;        // 64-bit to avoid overflow
    bool used;
} rate_limit_entry_t;

static rate_limit_entry_t *s_entries = NULL;
static uint32_t s_max_entries = 32;
static uint32_t s_max_requests = 20;
static uint32_t s_window_ms = 1000;
static uint32_t s_block_duration_sec = 5;
static SemaphoreHandle_t s_mutex = NULL;

static uint32_t s_total_hits = 0;
static uint32_t s_blocked_hits = 0;

static const rate_limiter_config_t s_default_config = {
    .max_requests = 20,
    .window_ms = 1000,
    .block_duration_sec = 5,
    .max_entries = 32
};

static int64_t get_time_us(void)
{
    // Use 64-bit esp_timer to avoid tick overflow (Fix #18)
    return esp_timer_get_time();
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
        return true;  // Default allow
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    s_total_hits++;

    rate_limit_entry_t *entry = find_or_create_entry(client_ip);

    if (entry == NULL) {
        // No free slot, reject
        s_blocked_hits++;
        xSemaphoreGive(s_mutex);
        ESP_LOGW(TAG, "Rate limit table full, rejecting %s", client_ip);
        return false;
    }

    int64_t now = get_time_us();

    // Initialize new entry
    if (!entry->used) {
        memset(entry, 0, sizeof(rate_limit_entry_t));
        strncpy(entry->ip, client_ip, MAX_IP_LENGTH - 1);
        entry->used = true;
        entry->window_start = now;
        entry->request_count = 1;
        xSemaphoreGive(s_mutex);
        return true;
    }

    // Check if blocked
    if (entry->blocked) {
        if (now < entry->block_until) {
            s_blocked_hits++;
            xSemaphoreGive(s_mutex);
            return false;  // Still blocked
        }
        // Block expired, reset
        entry->blocked = false;
        entry->request_count = 0;
        entry->window_start = now;
    }

    // Check time window (convert window_ms to microseconds)
    int64_t elapsed_us = now - entry->window_start;
    int64_t window_us = (int64_t)s_window_ms * 1000;
    if (elapsed_us > window_us) {
        // New window
        entry->request_count = 1;
        entry->window_start = now;
        xSemaphoreGive(s_mutex);
        return true;
    }

    // Increment counter within window
    entry->request_count++;

    if (entry->request_count > s_max_requests) {
        // Exceeded, block (convert block_duration to microseconds)
        entry->blocked = true;
        entry->block_until = now + (int64_t)s_block_duration_sec * 1000000;
        s_blocked_hits++;
        xSemaphoreGive(s_mutex);
        ESP_LOGW(TAG, "Rate limit exceeded for %s: %lu req in %lu ms",
                 client_ip, (unsigned long)entry->request_count,
                 (unsigned long)(elapsed_us / 1000));
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
        entry->window_start = get_time_us();
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
