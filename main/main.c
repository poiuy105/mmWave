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

#if defined(CONFIG_RADAR_LD6004)
#include "radar_ld6004.h"
#endif

static const char *TAG = "MAIN";
static const char *TAG_VERIFY = "LD6004_VERIFY";

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

#if defined(CONFIG_RADAR_LD6004)
// 前向声明 LD6004 验证任务
static void ld6004_verify_task(void *pvParameters);
#endif

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

#if defined(CONFIG_RADAR_LD6004)
    // 启动 LD6004 验证任务
    xTaskCreate(ld6004_verify_task, "ld6004_verify", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "LD6004 verification task started");
#endif

    // 主循环 - 打印系统状态
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000)); // 每 10 秒

        // 打印内存状态
        ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
        ESP_LOGI(TAG, "Min free heap: %lu bytes", esp_get_minimum_free_heap_size());
    }
}

#if defined(CONFIG_RADAR_LD6004)
/**
 * LD6004 雷达验证任务 - 验证数据解析和命令响应
 */
static void ld6004_verify_task(void *pvParameters)
{
    ESP_LOGI(TAG_VERIFY, "=================================");
    ESP_LOGI(TAG_VERIFY, "LD6004 Verification Task Started");
    ESP_LOGI(TAG_VERIFY, "=================================");

    // 等待雷达初始化完成
    vTaskDelay(pdMS_TO_TICKS(3000));

    ld6004_handle_t radar = radar_adapter_get_handle();
    if (radar == NULL) {
        ESP_LOGE(TAG_VERIFY, "Radar handle is NULL, cannot verify");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG_VERIFY, "[STEP 1] Radar handle obtained successfully");

    // Step 2: 查询固件版本
    ESP_LOGI(TAG_VERIFY, "[STEP 2] Querying firmware version...");
    esp_err_t err = ld6004_query_firmware_version(radar);
    if (err == ESP_OK) {
        ESP_LOGI(TAG_VERIFY, "[STEP 2] PASS: Firmware version query sent");
    } else {
        ESP_LOGE(TAG_VERIFY, "[STEP 2] FAIL: %s", esp_err_to_name(err));
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    // Step 3: 获取灵敏度
    ESP_LOGI(TAG_VERIFY, "[STEP 3] Getting sensitivity...");
    ld6004_sensitivity_t sens;
    err = ld6004_get_sensitivity(radar, &sens);
    if (err == ESP_OK) {
        ESP_LOGI(TAG_VERIFY, "[STEP 3] PASS: Sensitivity = %d (0=Low, 1=Mid, 2=High)", sens);
    } else {
        ESP_LOGE(TAG_VERIFY, "[STEP 3] FAIL: %s", esp_err_to_name(err));
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    // Step 4: 获取触发速度
    ESP_LOGI(TAG_VERIFY, "[STEP 4] Getting trigger speed...");
    ld6004_trigger_speed_t speed;
    err = ld6004_get_trigger_speed(radar, &speed);
    if (err == ESP_OK) {
        ESP_LOGI(TAG_VERIFY, "[STEP 4] PASS: Trigger speed = %d (0=Slow, 1=Mid, 2=Fast)", speed);
    } else {
        ESP_LOGE(TAG_VERIFY, "[STEP 4] FAIL: %s", esp_err_to_name(err));
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    // Step 5: 获取安装模式
    ESP_LOGI(TAG_VERIFY, "[STEP 5] Getting install mode...");
    ld6004_install_mode_t install;
    err = ld6004_get_install_mode(radar, &install);
    if (err == ESP_OK) {
        ESP_LOGI(TAG_VERIFY, "[STEP 5] PASS: Install mode = %d (0=Top, 1=Side)", install);
    } else {
        ESP_LOGE(TAG_VERIFY, "[STEP 5] FAIL: %s", esp_err_to_name(err));
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    // Step 6: 获取工作模式
    ESP_LOGI(TAG_VERIFY, "[STEP 6] Getting work mode...");
    ld6004_work_mode_t work;
    err = ld6004_get_work_mode(radar, &work);
    if (err == ESP_OK) {
        ESP_LOGI(TAG_VERIFY, "[STEP 6] PASS: Work mode = %d (0=Normal, 1=LowPower, 2=OffHigh, 3=OffLow, 4=StrongRefl)", work);
    } else {
        ESP_LOGE(TAG_VERIFY, "[STEP 6] FAIL: %s", esp_err_to_name(err));
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    // Step 7: 获取GPIO模式
    ESP_LOGI(TAG_VERIFY, "[STEP 7] Getting GPIO mode...");
    ld6004_gpio_mode_t gpio;
    err = ld6004_get_gpio_mode(radar, &gpio);
    if (err == ESP_OK) {
        ESP_LOGI(TAG_VERIFY, "[STEP 7] PASS: GPIO mode = %d", gpio);
    } else {
        ESP_LOGE(TAG_VERIFY, "[STEP 7] FAIL: %s", esp_err_to_name(err));
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    // Step 8: 设置灵敏度测试
    ESP_LOGI(TAG_VERIFY, "[STEP 8] Testing sensitivity set/get...");
    ld6004_sensitivity_t orig_sens = sens;
    err = ld6004_set_sensitivity(radar, LD6004_SENSITIVITY_HIGH);
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(500));
        err = ld6004_get_sensitivity(radar, &sens);
        if (err == ESP_OK && sens == LD6004_SENSITIVITY_HIGH) {
            ESP_LOGI(TAG_VERIFY, "[STEP 8] PASS: Sensitivity set to HIGH and verified");
        } else {
            ESP_LOGE(TAG_VERIFY, "[STEP 8] FAIL: Set OK but get returned %d", sens);
        }
        // Restore original
        ld6004_set_sensitivity(radar, orig_sens);
    } else {
        ESP_LOGE(TAG_VERIFY, "[STEP 8] FAIL: %s", esp_err_to_name(err));
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    // Step 9: 设置工作模式测试
    ESP_LOGI(TAG_VERIFY, "[STEP 9] Testing work mode set/get...");
    ld6004_work_mode_t orig_work = work;
    err = ld6004_set_work_mode(radar, LD6004_WORK_MODE_NORMAL);
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(500));
        err = ld6004_get_work_mode(radar, &work);
        if (err == ESP_OK && work == LD6004_WORK_MODE_NORMAL) {
            ESP_LOGI(TAG_VERIFY, "[STEP 9] PASS: Work mode set to NORMAL and verified");
        } else {
            ESP_LOGE(TAG_VERIFY, "[STEP 9] FAIL: Set OK but get returned %d", work);
        }
    } else {
        ESP_LOGE(TAG_VERIFY, "[STEP 9] FAIL: %s", esp_err_to_name(err));
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    // Step 10: 区域设置测试
    ESP_LOGI(TAG_VERIFY, "[STEP 10] Testing area set...");
    ld6004_area_t area = {
        .x_min = -2.0f,
        .x_max = 2.0f,
        .y_min = 0.0f,
        .y_max = 4.0f,
        .z_min = -1.5f,
        .z_max = 1.5f
    };
    err = ld6004_set_area(radar, &area);
    if (err == ESP_OK) {
        ESP_LOGI(TAG_VERIFY, "[STEP 10] PASS: Area set command accepted");
    } else {
        ESP_LOGE(TAG_VERIFY, "[STEP 10] FAIL: %s", esp_err_to_name(err));
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    // Step 11: 功能命令测试
    ESP_LOGI(TAG_VERIFY, "[STEP 11] Testing auto generate noise...");
    err = ld6004_auto_gen_noise(radar);
    if (err == ESP_OK) {
        ESP_LOGI(TAG_VERIFY, "[STEP 11] PASS: Auto generate noise command accepted");
    } else {
        ESP_LOGE(TAG_VERIFY, "[STEP 11] FAIL: %s", esp_err_to_name(err));
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    // Step 12: 重置检测状态
    ESP_LOGI(TAG_VERIFY, "[STEP 12] Testing reset detection...");
    err = ld6004_reset_detection(radar);
    if (err == ESP_OK) {
        ESP_LOGI(TAG_VERIFY, "[STEP 12] PASS: Reset detection command accepted");
    } else {
        ESP_LOGE(TAG_VERIFY, "[STEP 12] FAIL: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG_VERIFY, "=================================");
    ESP_LOGI(TAG_VERIFY, "LD6004 Verification Complete");
    ESP_LOGI(TAG_VERIFY, "=================================");

    vTaskDelete(NULL);
}
#endif
