/**
 * LD Radar Web Monitor - 主程序入口
 * ESP32-C3 嵌入式 Web 服务器监控系统
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"

#include "web_server/fatfs_init.h"
#include "web_server/http_server.h"
#include "web_server/file_manager.h"
#include "radar_adapter/radar_adapter.h"
#include "radar_test/radar_test.h"

static const char *TAG = "MAIN";

// WiFi 配置
#define WIFI_SSID      "FUCK_cao"
#define WIFI_PASS      "fuckyou001"
#define WIFI_CHANNEL   1
#define WIFI_MAX_CONN  4

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;
#define MAX_RETRY  5

/**
 * WiFi event handler - STA mode only (Fix #26: removed unused AP events)
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry to connect to the AP (%d/%d)", s_retry_num, MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "WiFi connection failed after %d retries", MAX_RETRY);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/**
 * 初始化 WiFi STA 模式（连接路由器）
 */
static void wifi_init_sta(void)
{
    // 创建事件组
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi STA initialized, connecting to SSID:%s", WIFI_SSID);
}

/**
 * 初始化 NVS
 */
static esp_err_t nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

/**
 * 主程序入口
 */
void app_main(void)
{
    ESP_LOGI(TAG, "=================================");
    ESP_LOGI(TAG, "LD Radar Web Monitor Starting...");
    ESP_LOGI(TAG, "Version: 1.0.0");
    ESP_LOGI(TAG, "=================================");

    // 初始化 NVS
    ESP_ERROR_CHECK(nvs_init());
    ESP_LOGI(TAG, "NVS initialized");

    // 初始化 FATFS
    ESP_ERROR_CHECK(fatfs_init());
    ESP_LOGI(TAG, "FATFS initialized");

    // 初始化文件管理器
    ESP_ERROR_CHECK(file_manager_init());
    ESP_LOGI(TAG, "File manager initialized");

    // 初始化雷达适配层
    esp_err_t radar_err = radar_adapter_init();
    if (radar_err == ESP_OK) {
        radar_info_t radar_info;
        radar_adapter_get_info(&radar_info);
        ESP_LOGI(TAG, "Radar initialized: %s", radar_info.name);
    } else {
        ESP_LOGW(TAG, "Radar initialization failed: %s (continuing without radar)", esp_err_to_name(radar_err));
    }

    // Initialize WiFi STA mode
    wifi_init_sta();

    // Wait for WiFi connection with timeout (Fix #27)
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                            pdFALSE, pdFALSE,
                                            pdMS_TO_TICKS(15000));  // 15s timeout

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected successfully");
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGW(TAG, "WiFi connection failed, starting HTTP server in AP-less mode");
    } else {
        ESP_LOGW(TAG, "WiFi connection timeout, starting HTTP server anyway");
    }

    // Start HTTP server (Fix #27: start after WiFi attempt)
    ESP_ERROR_CHECK(http_server_start());
    ESP_LOGI(TAG, "HTTP server started");

    ESP_LOGI(TAG, "=================================");
    ESP_LOGI(TAG, "System Ready!");
    ESP_LOGI(TAG, "=================================");

    // 运行雷达控制命令验证测试（仅 LD2460）
    // 测试会在启动后 5 秒执行，验证底层驱动写命令
    vTaskDelay(pdMS_TO_TICKS(5000));
    ESP_LOGI(TAG, "Running radar control command verification test...");
    esp_err_t test_err = radar_test_run_all();
    if (test_err == ESP_OK) {
        ESP_LOGI(TAG, "Radar control test completed successfully");
    } else {
        ESP_LOGW(TAG, "Radar control test failed or skipped: %s", esp_err_to_name(test_err));
    }

    // 主循环 - 打印系统状态
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000)); // 每 10 秒

        // 打印内存状态
        ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
        ESP_LOGI(TAG, "Min free heap: %lu bytes", esp_get_minimum_free_heap_size());
    }
}
