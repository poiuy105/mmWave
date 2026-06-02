/**
 * LD Radar Web Monitor - 主程序入口
 * 状态机驱动架构：SoftAP 配置门户 + STA 运行模式
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
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_server.h"

#include "config/nvs_config.h"
#include "mqtt/app_mqtt.h"
#include "wifi/wifi_manager.h"
#include "wifi/softap.h"
#include "core/state_machine.h"
#include "drivers/gpio_control.h"
#include "utils/system_info.h"
#include "web_server/fatfs_init.h"
#include "web_server/http_server.h"
#include "web_server/file_manager.h"
#include "web_server/dns_server.h"
#include "web_server/portal_handlers.h"
#include "radar_adapter/radar_adapter.h"
#include "radar_test/radar_test.h"

static const char *TAG = "MAIN";

// ============ 常量定义 ============
#define SOFTAP_TIMEOUT_MS     (5 * 60 * 1000)  // SoftAP 超时 5 分钟
#define WIFI_CONNECT_TIMEOUT_MS  20000         // WiFi 单次连接超时 20s
#define MQTT_CONNECT_TIMEOUT_MS  15000         // MQTT 连接超时 15s
#define MAIN_LOOP_INTERVAL_MS     10000         // 主循环间隔 10s
#define RADAR_TEST_DELAY_MS       5000          // 雷达测试延迟 5s

// ============ 全局变量 ============
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT     BIT1

static app_config_t s_app_config = {0};
static httpd_handle_t s_portal_server = NULL;
static bool s_auto_connect = true;

// ============ WiFi 事件处理 ============

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (s_auto_connect) {
            esp_err_t ret = esp_wifi_connect();
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "WiFi connect failed: %s", esp_err_to_name(ret));
            }
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected");
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// ============ 网络初始化（仅一次） ============

static void network_init(void)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));
}

// ============ SoftAP 配置门户 ============

static void start_portal_server(void)
{
    if (s_portal_server != NULL) return;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.max_uri_handlers = 16;

    if (httpd_start(&s_portal_server, &config) == ESP_OK) {
        portal_handlers_register(s_portal_server);
        ESP_LOGI(TAG, "Portal HTTP server started on port %d", config.server_port);
    } else {
        ESP_LOGE(TAG, "Failed to start portal HTTP server");
    }
}

static void stop_portal_server(void)
{
    if (s_portal_server != NULL) {
        httpd_stop(s_portal_server);
        s_portal_server = NULL;
        ESP_LOGI(TAG, "Portal HTTP server stopped");
    }
}

static void run_state_softap(void)
{
    ESP_LOGI(TAG, ">>> Entering SOFTAP configuration mode");

    // 生成 SSID
    char softap_ssid[32];
    softap_generate_ssid_with_mac(softap_ssid, sizeof(softap_ssid));

    // 禁止 STA 自动连接
    s_auto_connect = false;

    // 启动 SoftAP
    esp_err_t err = wifi_manager_start_softap(softap_ssid, "");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start SoftAP: %s", esp_err_to_name(err));
        state_machine_trigger_event(EVENT_TIMEOUT);
        return;
    }

    // 启动 DNS 劫持 + HTTP 配置服务器
    dns_server_start(53, "192.168.4.1");
    start_portal_server();

    ESP_LOGI(TAG, "SoftAP '%s' started, waiting for configuration...", softap_ssid);
    ESP_LOGI(TAG, "Connect to '%s' and open http://192.168.4.1", softap_ssid);

    // 超时循环：5 分钟
    uint32_t start_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    while (state_machine_get_state() == STATE_SOFTAP) {
        uint32_t elapsed = (xTaskGetTickCount() * portTICK_PERIOD_MS) - start_ms;
        if (elapsed > SOFTAP_TIMEOUT_MS) {
            ESP_LOGW(TAG, "SoftAP timeout (%d min), restarting...", SOFTAP_TIMEOUT_MS / 60000);
            esp_restart();
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // 状态已转换（用户提交了配置），清理 SoftAP 资源
    ESP_LOGI(TAG, "Leaving SOFTAP mode, cleaning up...");
    stop_portal_server();
    dns_server_stop();
    wifi_manager_stop_softap();
    vTaskDelay(pdMS_TO_TICKS(1000));
}

// ============ WiFi 连接（指数退避） ============

static bool connect_wifi_with_retry(void)
{
    int retry_delay_ms = 5000;
    const int max_retry_delay_ms = 60000;
    int max_attempts = 5;

    for (int attempt = 0; attempt < max_attempts; attempt++) {
        ESP_LOGI(TAG, "WiFi connect attempt %d/%d (SSID: %s)", 
                 attempt + 1, max_attempts, s_app_config.wifi_ssid);

        // 清除事件位
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

        esp_err_t err = wifi_manager_connect_sta(s_app_config.wifi_ssid, s_app_config.wifi_password);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "WiFi connect failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
            retry_delay_ms = (retry_delay_ms * 2 < max_retry_delay_ms) ? retry_delay_ms * 2 : max_retry_delay_ms;
            continue;
        }

        // 等待连接结果
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                                WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                                pdTRUE, pdFALSE,
                                                pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "WiFi connected successfully");
            state_machine_trigger_event(EVENT_WIFI_CONNECTED);
            return true;
        }

        // 连接失败，停止 STA 再重试
        ESP_LOGW(TAG, "WiFi connect timeout/fail, retrying in %dms", retry_delay_ms);
        wifi_manager_stop_sta();
        vTaskDelay(pdMS_TO_TICKS(1000));
        vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
        retry_delay_ms = (retry_delay_ms * 2 < max_retry_delay_ms) ? retry_delay_ms * 2 : max_retry_delay_ms;
    }

    ESP_LOGE(TAG, "WiFi connection failed after %d attempts", max_attempts);
    return false;
}

static void run_state_config(void)
{
    ESP_LOGI(TAG, ">>> Entering CONFIG state (connecting WiFi)");

    // 允许 STA 自动连接
    s_auto_connect = true;

    // 重新加载配置（可能刚被用户更新）
    nvs_load_all_config(&s_app_config);

    if (!connect_wifi_with_retry()) {
        ESP_LOGW(TAG, "WiFi connection failed, returning to SOFTAP");
        state_machine_trigger_event(EVENT_TIMEOUT);
    }
    // 如果连接成功，状态机已转到 STATE_CONNECTING
}

// ============ MQTT 连接 ============

// MQTT连接状态跟踪（文件级静态变量，跨函数调用保持状态）
static bool s_mqtt_connecting = false;
static uint32_t s_mqtt_connect_start_time = 0;

// 运行状态跟踪
static bool s_running_initialized = false;
static uint32_t s_system_info_report_tick = 0;

static void run_state_connecting(void)
{
    ESP_LOGI(TAG, ">>> Entering CONNECTING state (connecting MQTT)");

    // 检查是否已连接
    if (app_mqtt_is_connected()) {
        ESP_LOGI(TAG, "MQTT already connected, transition to RUNNING");
        s_mqtt_connecting = false;
        state_machine_trigger_event(EVENT_MQTT_CONNECTED);
        return;
    }

    // 检查是否正在连接中
    if (s_mqtt_connecting) {
        uint32_t elapsed = (xTaskGetTickCount() * portTICK_PERIOD_MS) - s_mqtt_connect_start_time;
        
        // 检查是否超时
        if (elapsed > MQTT_CONNECT_TIMEOUT_MS) {
            ESP_LOGW(TAG, "MQTT connection timeout (%lu ms), entering RUNNING", elapsed);
            s_mqtt_connecting = false;
            state_machine_trigger_event(EVENT_MQTT_CONNECTED); // 超时也进入RUNNING
            return;
        }
        
        // 延时等待，避免 tight loop
        vTaskDelay(pdMS_TO_TICKS(1000));
        return;
    }

    // 发起新连接
    s_mqtt_connecting = true;
    s_mqtt_connect_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    esp_err_t err = app_mqtt_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MQTT init failed, skipping MQTT");
        s_mqtt_connecting = false;
        state_machine_trigger_event(EVENT_MQTT_CONNECTED);
        return;
    }

    mqtt_config_t mqtt_cfg = {0};
    strncpy(mqtt_cfg.uri, s_app_config.mqtt_uri, sizeof(mqtt_cfg.uri) - 1);
    mqtt_cfg.port = s_app_config.mqtt_port;
    strncpy(mqtt_cfg.username, s_app_config.mqtt_username, sizeof(mqtt_cfg.username) - 1);
    strncpy(mqtt_cfg.password, s_app_config.mqtt_password, sizeof(mqtt_cfg.password) - 1);

    err = app_mqtt_connect(&mqtt_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MQTT connect failed: %s", esp_err_to_name(err));
        s_mqtt_connecting = false;
        state_machine_trigger_event(EVENT_MQTT_CONNECTED);
        return;
    }

    ESP_LOGI(TAG, "MQTT connection initiated, waiting for callback...");
    // 不在这里等待，让MQTT事件回调触发状态转换
}

// ============ 正常运行 ============

static void run_state_running(void)
{
    ESP_LOGI(TAG, ">>> Entering RUNNING state");

    // 初始化 GPIO 控制（LED）
    esp_err_t gpio_err = gpio_control_init();
    if (gpio_err == ESP_OK) {
        ESP_LOGI(TAG, "GPIO control initialized");
    } else {
        ESP_LOGW(TAG, "GPIO control init failed: %s", esp_err_to_name(gpio_err));
    }

    // 初始化系统信息监控
    system_info_init();

    // 启动主 HTTP 服务器（雷达 WebSocket + REST API）
    ESP_ERROR_CHECK(http_server_start());
    ESP_LOGI(TAG, "Main HTTP server started");

    // 雷达控制命令验证测试
    vTaskDelay(pdMS_TO_TICKS(RADAR_TEST_DELAY_MS));
    esp_err_t test_err = radar_test_run_all();
    if (test_err == ESP_OK) {
        ESP_LOGI(TAG, "Radar control test passed");
    } else {
        ESP_LOGW(TAG, "Radar control test: %s", esp_err_to_name(test_err));
    }

    ESP_LOGI(TAG, "=================================");
    ESP_LOGI(TAG, "System Ready!");
    ESP_LOGI(TAG, "=================================");

    // 主循环
    uint32_t report_count = 0;
    while (state_machine_get_state() == STATE_RUNNING) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        report_count++;

        // 每 30 秒上报系统信息
        if (report_count >= 30) {
            report_count = 0;
            ESP_LOGI(TAG, "Free heap: %lu bytes, min: %lu bytes",
                     (unsigned long)esp_get_free_heap_size(),
                     (unsigned long)esp_get_minimum_free_heap_size());
            if (app_mqtt_is_connected()) {
                app_mqtt_publish_system_info();
            }
        }

        // 检测 WiFi 断开
        EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
        if (bits & WIFI_FAIL_BIT) {
            ESP_LOGW(TAG, "WiFi disconnected in running state");
            state_machine_trigger_event(EVENT_WIFI_DISCONNECTED);
        }
    }

    // 退出 RUNNING 状态，停止 HTTP 服务器
    http_server_stop();
    ESP_LOGI(TAG, "Main HTTP server stopped");
}

// ============ 主任务（状态机驱动） ============

static void app_task(void *pvParameters)
{
    ESP_LOGI(TAG, "App task started");

    while (1) {
        app_state_t state = state_machine_get_state();

        switch (state) {
            case STATE_INIT:
                // 根据配置状态决定初始路径
                if (s_app_config.is_configured && !s_app_config.first_boot) {
                    ESP_LOGI(TAG, "Already configured, skipping SoftAP");
                    state_machine_trigger_event(EVENT_CONFIG_RECEIVED);
                } else {
                    state_machine_trigger_event(EVENT_INIT_COMPLETE);
                }
                break;

            case STATE_SOFTAP:
                run_state_softap();
                break;

            case STATE_CONFIG:
                run_state_config();
                break;

            case STATE_CONNECTING:
                run_state_connecting();
                // 检查MQTT是否已连接（由事件回调触发）
                if (app_mqtt_is_connected() && s_mqtt_connecting) {
                    ESP_LOGI(TAG, "MQTT connected detected in app_task");
                    s_mqtt_connecting = false;
                    app_mqtt_publish_ha_discovery();
                    state_machine_trigger_event(EVENT_MQTT_CONNECTED);
                }
                break;

            case STATE_RUNNING:
                // 首次进入RUNNING状态，执行初始化
                if (!s_running_initialized) {
                    run_state_running();
                    s_running_initialized = true;
                    s_system_info_report_tick = 0;
                }
                
                // 定时发布系统信息（每30秒）
                if (app_mqtt_is_connected()) {
                    s_system_info_report_tick++;
                    if (s_system_info_report_tick >= 30) {
                        s_system_info_report_tick = 0;
                        app_mqtt_publish_system_info();
                    }
                }
                vTaskDelay(pdMS_TO_TICKS(1000));
                break;

            case STATE_ERROR:
                ESP_LOGE(TAG, "Error state, retrying...");
                vTaskDelay(pdMS_TO_TICKS(5000));
                state_machine_trigger_event(EVENT_TIMEOUT);
                break;

            default:
                ESP_LOGE(TAG, "Unknown state: %d", state);
                vTaskDelay(pdMS_TO_TICKS(1000));
                break;
        }
    }
}

// ============ 入口 ============

void app_main(void)
{
    ESP_LOGI(TAG, "=================================");
    ESP_LOGI(TAG, "LD Radar Web Monitor v2.0");
    ESP_LOGI(TAG, "State Machine Architecture");
    ESP_LOGI(TAG, "=================================");

    // 1. NVS 初始化
    esp_err_t nvs_err = nvs_init_config();
    if (nvs_err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(nvs_err));
        nvs_flash_erase();
        nvs_err = nvs_flash_init();
        if (nvs_err != ESP_OK) {
            ESP_LOGE(TAG, "NVS fatal error, restarting...");
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_restart();
        }
    }

    // 2. 加载配置
    nvs_load_all_config(&s_app_config);
    ESP_LOGI(TAG, "Config: SSID=%s, MQTT=%s:%d, configured=%d",
             s_app_config.wifi_ssid, s_app_config.mqtt_uri, 
             s_app_config.mqtt_port, s_app_config.is_configured);

    // 3. FATFS 初始化
    ESP_ERROR_CHECK(fatfs_init());
    ESP_ERROR_CHECK(file_manager_init());

    // 4. 雷达初始化
    esp_err_t radar_err = radar_adapter_init();
    if (radar_err == ESP_OK) {
        radar_info_t info;
        if (radar_adapter_get_info(&info) == ESP_OK) {
            ESP_LOGI(TAG, "Radar: %s", info.name);
        }
    } else {
        ESP_LOGW(TAG, "Radar init failed: %s", esp_err_to_name(radar_err));
    }

    // 5. 网络初始化
    network_init();

    // 6. 状态机初始化
    state_machine_init();

    // 7. 创建主任务（状态机驱动循环）
    xTaskCreate(app_task, "app_task", 4096, NULL, 5, NULL);
}
