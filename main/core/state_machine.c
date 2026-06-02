#include "state_machine.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "state_machine";

static app_state_t s_state = STATE_INIT;
static uint32_t s_state_enter_time = 0;

static const char *s_state_names[] = {
    "INIT", "SOFTAP", "CONFIG", "CONNECTING", "RUNNING", "ERROR"
};

static const char *s_event_names[] = {
    "INIT_COMPLETE", "CONFIG_RECEIVED", "WIFI_CONNECTED", "WIFI_DISCONNECTED",
    "MQTT_CONNECTED", "MQTT_DISCONNECTED", "RESET_CONFIG", "TIMEOUT"
};

// ============ 前向声明 ============
static void handle_state_init(app_event_t event);
static void handle_state_softap(app_event_t event);
static void handle_state_config(app_event_t event);
static void handle_state_connecting(app_event_t event);
static void handle_state_running(app_event_t event);
static void handle_state_error(app_event_t event);

// ============ 状态转换 ============
static void transition_to(app_state_t new_state)
{
    if (s_state != new_state) {
        ESP_LOGI(TAG, "State: %s -> %s", s_state_names[s_state], s_state_names[new_state]);
        s_state = new_state;
        s_state_enter_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    }
}

// ============ 各状态事件处理 ============

static void handle_state_init(app_event_t event)
{
    switch (event) {
        case EVENT_INIT_COMPLETE:
            transition_to(STATE_SOFTAP);
            break;
        case EVENT_CONFIG_RECEIVED:
            // 已有配置，跳过 SOFTAP 直接连 WiFi
            transition_to(STATE_CONFIG);
            break;
        default:
            break;
    }
}

static void handle_state_softap(app_event_t event)
{
    switch (event) {
        case EVENT_CONFIG_RECEIVED:
            transition_to(STATE_CONFIG);
            break;
        case EVENT_RESET_CONFIG:
            transition_to(STATE_INIT);
            break;
        default:
            break;
    }
}

static void handle_state_config(app_event_t event)
{
    switch (event) {
        case EVENT_WIFI_CONNECTED:
            transition_to(STATE_CONNECTING);
            break;
        case EVENT_WIFI_DISCONNECTED:
        case EVENT_TIMEOUT:
            // WiFi 连接失败，回到 SOFTAP 让用户重新配置
            transition_to(STATE_SOFTAP);
            break;
        default:
            break;
    }
}

static void handle_state_connecting(app_event_t event)
{
    switch (event) {
        case EVENT_MQTT_CONNECTED:
            transition_to(STATE_RUNNING);
            break;
        case EVENT_WIFI_DISCONNECTED:
            // WiFi 掉了，回退重连 WiFi
            transition_to(STATE_CONFIG);
            break;
        case EVENT_TIMEOUT:
            // 保持 CONNECTING，ESP-MQTT 库自己会重连
            ESP_LOGW(TAG, "MQTT connect timeout, keep retrying");
            break;
        default:
            break;
    }
}

static void handle_state_running(app_event_t event)
{
    switch (event) {
        case EVENT_WIFI_DISCONNECTED:
            transition_to(STATE_CONFIG);
            break;
        case EVENT_MQTT_DISCONNECTED:
            // 不改变状态，ESP-MQTT 库自动重连
            ESP_LOGW(TAG, "MQTT disconnected, auto-reconnect");
            break;
        default:
            break;
    }
}

static void handle_state_error(app_event_t event)
{
    switch (event) {
        case EVENT_WIFI_CONNECTED:
            transition_to(STATE_CONNECTING);
            break;
        case EVENT_RESET_CONFIG:
            transition_to(STATE_INIT);
            break;
        case EVENT_TIMEOUT:
            transition_to(STATE_CONFIG);
            break;
        default:
            break;
    }
}

// ============ 公共接口 ============

esp_err_t state_machine_init(void)
{
    s_state = STATE_INIT;
    s_state_enter_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    ESP_LOGI(TAG, "State machine initialized");
    return ESP_OK;
}

app_state_t state_machine_get_state(void)
{
    return s_state;
}

const char *state_machine_get_state_name(app_state_t state)
{
    if (state >= STATE_ERROR) return "UNKNOWN";
    return s_state_names[state];
}

void state_machine_trigger_event(app_event_t event)
{
    if (event < EVENT_INIT_COMPLETE || event > EVENT_TIMEOUT) {
        ESP_LOGE(TAG, "Invalid event: %d", event);
        return;
    }

    ESP_LOGI(TAG, "Event: %s (current state: %s)", 
             s_event_names[event], s_state_names[s_state]);

    switch (s_state) {
        case STATE_INIT:       handle_state_init(event);       break;
        case STATE_SOFTAP:     handle_state_softap(event);     break;
        case STATE_CONFIG:     handle_state_config(event);     break;
        case STATE_CONNECTING: handle_state_connecting(event); break;
        case STATE_RUNNING:    handle_state_running(event);    break;
        case STATE_ERROR:      handle_state_error(event);      break;
        default:
            ESP_LOGE(TAG, "Unknown state: %d", s_state);
            break;
    }
}

void state_machine_reset(void)
{
    ESP_LOGI(TAG, "State machine reset");
    s_state = STATE_INIT;
    s_state_enter_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
}
