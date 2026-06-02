#include <string.h>
#include "app_mqtt.h"
#include "drivers/gpio_control.h"
#include "utils/system_info.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "cJSON.h"

static const char *TAG = "mqtt_client";

static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static mqtt_state_t s_mqtt_state = MQTT_STATE_DISCONNECTED;
static SemaphoreHandle_t s_mqtt_mutex = NULL;
static bool s_mqtt_initialized = false;  // 一次性初始化标志

// Node ID (MAC-based)
static char s_node_id[32] = {0};
static char s_lwt_topic[64] = {0};

// LED 控制 Topic
static char s_led_state_topic[64] = {0};
static char s_led_cmd_topic[64] = {0};

// Helper macros for mutex
#define MQTT_LOCK()   do { if (s_mqtt_mutex) xSemaphoreTake(s_mqtt_mutex, pdMS_TO_TICKS(100)); } while(0)
#define MQTT_UNLOCK() do { if (s_mqtt_mutex) xSemaphoreGive(s_mqtt_mutex); } while(0)

// Initialize node_id from MAC
static void init_node_id(void)
{
    if (strlen(s_node_id) > 0) return;
    
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    
    snprintf(s_node_id, sizeof(s_node_id), "ldradar_%02x%02x%02x%02x",
             mac[2], mac[3], mac[4], mac[5]);
    snprintf(s_lwt_topic, sizeof(s_lwt_topic), "%s/status", s_node_id);
    snprintf(s_led_state_topic, sizeof(s_led_state_topic), "%s/led/state", s_node_id);
    snprintf(s_led_cmd_topic, sizeof(s_led_cmd_topic), "%s/led/set", s_node_id);
    
    ESP_LOGI(TAG, "Node ID: %s", s_node_id);
    ESP_LOGI(TAG, "LWT topic: %s", s_lwt_topic);
    ESP_LOGI(TAG, "LED state topic: %s", s_led_state_topic);
    ESP_LOGI(TAG, "LED cmd topic: %s", s_led_cmd_topic);
}

// MQTT event handler
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, 
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            MQTT_LOCK();
            s_mqtt_state = MQTT_STATE_CONNECTED;
            MQTT_UNLOCK();
            
            // Publish online status
            app_mqtt_publish_status("online");
            
            // Subscribe to LED control topic
            init_node_id();
            esp_mqtt_client_subscribe(s_mqtt_client, s_led_cmd_topic, 1);
            ESP_LOGI(TAG, "Subscribed to LED command topic: %s", s_led_cmd_topic);
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            MQTT_LOCK();
            s_mqtt_state = MQTT_STATE_DISCONNECTED;
            MQTT_UNLOCK();
            break;
            
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGD(TAG, "MQTT subscribed, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "MQTT published, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error");
            MQTT_LOCK();
            s_mqtt_state = MQTT_STATE_ERROR;
            MQTT_UNLOCK();
            break;
            
        case MQTT_EVENT_DATA:
            // Handle incoming MQTT message
            {
                char topic[128] = {0};
                char data[32] = {0};
                
                if (event->topic_len < sizeof(topic) && event->data_len < sizeof(data)) {
                    strncpy(topic, event->topic, event->topic_len);
                    strncpy(data, event->data, event->data_len);
                    
                    ESP_LOGI(TAG, "MQTT data received: topic=%s, data=%s", topic, data);
                    
                    // Check if it's LED command
                    if (strcmp(topic, s_led_cmd_topic) == 0) {
                        if (strcasecmp(data, "ON") == 0) {
                            gpio_control_set_led(true);
                            app_mqtt_publish_led_state(true);
                        } else if (strcasecmp(data, "OFF") == 0) {
                            gpio_control_set_led(false);
                            app_mqtt_publish_led_state(false);
                        }
                    }
                }
            }
            break;
            
        default:
            break;
    }
}

esp_err_t app_mqtt_init(void)
{
    if (s_mqtt_mutex == NULL) {
        s_mqtt_mutex = xSemaphoreCreateMutex();
        if (s_mqtt_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create mutex");
            return ESP_ERR_NO_MEM;
        }
    }
    
    init_node_id();
    // 不要重置状态，保持当前连接状态
    if (s_mqtt_state == MQTT_STATE_DISCONNECTED) {
        ESP_LOGI(TAG, "MQTT module initialized");
    }
    return ESP_OK;
}

esp_err_t app_mqtt_connect(const mqtt_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid config");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 一次性初始化：如果已经创建过客户端，后续调用全部 no-op
    // ESP-MQTT 库内部有自动重连机制，不需要应用层重复创建连接
    if (s_mqtt_initialized) {
        ESP_LOGI(TAG, "MQTT client already initialized (state=%d), relying on auto-reconnect", s_mqtt_state);
        return ESP_OK;
    }
    
    s_mqtt_initialized = true;
    
    ESP_LOGI(TAG, "Connecting to MQTT: %s:%d", config->uri, config->port);
    
    init_node_id();
    
    // Parse URI to extract hostname
    char hostname[128] = {0};
    const char *uri_start = config->uri;
    
    if (strncmp(config->uri, "mqtt://", 7) == 0) {
        uri_start = config->uri + 7;
    } else if (strncmp(config->uri, "mqtts://", 8) == 0) {
        uri_start = config->uri + 8;
    }
    
    strncpy(hostname, uri_start, sizeof(hostname) - 1);
    char *colon = strchr(hostname, ':');
    if (colon) *colon = '\0';
    
    // Configure MQTT client
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address.hostname = hostname,
            .address.port = config->port,
            .address.transport = MQTT_TRANSPORT_OVER_TCP,
        },
        .credentials = {
            .client_id = s_node_id,  // 显式设置Client ID，避免重复
            .username = config->username,
            .authentication.password = config->password,
        },
        .session = {
            .last_will = {
                .topic = s_lwt_topic,
                .msg = "offline",
                .qos = 1,
                .retain = true,
            },
            .keepalive = 30,
        },
        .network = {
            .reconnect_timeout_ms = 5000,  // 增加到5秒，避免频繁重连
            .disable_auto_reconnect = false,
        },
    };
    
    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to init MQTT client");
        return ESP_ERR_NO_MEM;
    }
    
    esp_err_t err = esp_mqtt_client_register_event(s_mqtt_client, 
                                                    ESP_EVENT_ANY_ID,
                                                    mqtt_event_handler,
                                                    NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register event handler");
        esp_mqtt_client_destroy(s_mqtt_client);
        s_mqtt_client = NULL;
        return err;
    }
    
    err = esp_mqtt_client_start(s_mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client");
        esp_mqtt_client_destroy(s_mqtt_client);
        s_mqtt_client = NULL;
        return err;
    }
    
    MQTT_LOCK();
    s_mqtt_state = MQTT_STATE_CONNECTING;
    MQTT_UNLOCK();
    
    ESP_LOGI(TAG, "MQTT client started");
    return ESP_OK;
}

esp_err_t app_mqtt_disconnect(void)
{
    if (s_mqtt_client == NULL) {
        return ESP_OK;
    }
    
    // Publish offline status before disconnect
    app_mqtt_publish_status("offline");
    
    esp_mqtt_client_stop(s_mqtt_client);
    esp_mqtt_client_destroy(s_mqtt_client);
    s_mqtt_client = NULL;
    
    MQTT_LOCK();
    s_mqtt_state = MQTT_STATE_DISCONNECTED;
    MQTT_UNLOCK();
    
    ESP_LOGI(TAG, "MQTT disconnected");
    return ESP_OK;
}

void app_mqtt_deinit(void)
{
    app_mqtt_disconnect();
    
    if (s_mqtt_mutex) {
        vSemaphoreDelete(s_mqtt_mutex);
        s_mqtt_mutex = NULL;
    }
}

mqtt_state_t app_mqtt_get_state(void)
{
    MQTT_LOCK();
    mqtt_state_t state = s_mqtt_state;
    MQTT_UNLOCK();
    return state;
}

bool app_mqtt_is_connected(void)
{
    return app_mqtt_get_state() == MQTT_STATE_CONNECTED;
}

esp_err_t app_mqtt_publish(const char *topic, const char *data, int len, 
                           int qos, bool retain)
{
    if (s_mqtt_client == NULL || !app_mqtt_is_connected()) {
        ESP_LOGW(TAG, "MQTT not connected, skip publish");
        return ESP_ERR_INVALID_STATE;
    }
    
    int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, data, len, qos, retain);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "Publish failed: topic=%s", topic);
        return ESP_FAIL;
    }
    
    ESP_LOGD(TAG, "Published: topic=%s, msg_id=%d", topic, msg_id);
    return ESP_OK;
}

esp_err_t app_mqtt_publish_radar_frame(const radar_frame_t *frame)
{
    if (frame == NULL || !app_mqtt_is_connected()) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Build JSON payload
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "radar_data");
    cJSON_AddNumberToObject(root, "timestamp", frame->timestamp_ms);
    cJSON_AddNumberToObject(root, "frame_id", frame->frame_id);
    cJSON_AddNumberToObject(root, "target_count", frame->target_count);
    
    cJSON *targets = cJSON_CreateArray();
    for (int i = 0; i < frame->target_count && i < RADAR_MAX_TARGETS; i++) {
        cJSON *target = cJSON_CreateObject();
        cJSON_AddNumberToObject(target, "id", frame->targets[i].id);
        cJSON_AddNumberToObject(target, "x", frame->targets[i].x);
        cJSON_AddNumberToObject(target, "y", frame->targets[i].y);
        cJSON_AddNumberToObject(target, "z", frame->targets[i].z);
        cJSON_AddNumberToObject(target, "speed", frame->targets[i].speed);
        cJSON_AddNumberToObject(target, "snr", frame->targets[i].snr);
        cJSON_AddNumberToObject(target, "confidence", frame->targets[i].confidence);
        cJSON_AddItemToArray(targets, target);
    }
    cJSON_AddItemToObject(root, "targets", targets);
    
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (json_str == NULL) {
        ESP_LOGW(TAG, "Failed to create JSON");
        return ESP_ERR_NO_MEM;
    }
    
    // Build topic: {node_id}/radar/state
    char topic[64];
    snprintf(topic, sizeof(topic), "%s/radar/state", s_node_id);
    
    esp_err_t err = app_mqtt_publish(topic, json_str, 0, 0, false);
    free(json_str);
    
    return err;
}

esp_err_t app_mqtt_publish_system_info(void)
{
    if (!app_mqtt_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }
    
    system_info_t info;
    system_info_get(&info);
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "free_heap", info.free_heap);
    cJSON_AddNumberToObject(root, "min_free_heap", info.min_free_heap);
    cJSON_AddNumberToObject(root, "uptime_seconds", info.uptime_seconds);
    cJSON_AddNumberToObject(root, "wifi_rssi", info.wifi_rssi);
    cJSON_AddStringToObject(root, "wifi_ip", info.wifi_ip);
    cJSON_AddBoolToObject(root, "wifi_connected", info.wifi_connected);
    cJSON_AddNumberToObject(root, "wifi_channel", info.wifi_channel);
    
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (json_str == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    char topic[64];
    snprintf(topic, sizeof(topic), "%s/system/state", s_node_id);
    
    esp_err_t err = app_mqtt_publish(topic, json_str, 0, 0, false);
    free(json_str);
    
    return err;
}

esp_err_t app_mqtt_publish_status(const char *status)
{
    if (!app_mqtt_is_connected() && strcmp(status, "offline") != 0) {
        return ESP_ERR_INVALID_STATE;
    }
    
    char topic[64];
    snprintf(topic, sizeof(topic), "%s/status", s_node_id);
    
    // Use retain for status messages
    return app_mqtt_publish(topic, status, 0, 1, true);
}

esp_err_t app_mqtt_publish_ha_discovery(void)
{
    if (!app_mqtt_is_connected()) {
        ESP_LOGW(TAG, "MQTT not connected, skip HA discovery");
        return ESP_ERR_INVALID_STATE;
    }
    
    char config_topic[128];
    char state_topic[64];
    char *json_str;
    cJSON *config;
    cJSON *device;
    esp_err_t err = ESP_OK;
    
    // Availability topic (所有实体共用)
    // LWT: 设备断电时 Broker 自动发布 "offline" 到此 topic
    // 连接时: 主动发布 "online"
    const char *avail_topic = s_lwt_topic;
    
    snprintf(state_topic, sizeof(state_topic), "%s/radar/state", s_node_id);
    
    // ---- Sensor: target count ----
    snprintf(config_topic, sizeof(config_topic), 
             "homeassistant/sensor/%s/target_count/config", s_node_id);
    config = cJSON_CreateObject();
    cJSON_AddStringToObject(config, "name", "Radar Target Count");
    cJSON_AddStringToObject(config, "state_topic", state_topic);
    cJSON_AddStringToObject(config, "value_template", "{{ value_json.target_count }}");
    cJSON_AddStringToObject(config, "unit_of_measurement", "targets");
    cJSON_AddStringToObject(config, "unique_id", s_node_id);
    cJSON_AddStringToObject(config, "availability_topic", avail_topic);
    cJSON_AddStringToObject(config, "payload_available", "online");
    cJSON_AddStringToObject(config, "payload_not_available", "offline");
    device = cJSON_CreateObject();
    cJSON_AddStringToObject(device, "identifiers", s_node_id);
    cJSON_AddStringToObject(device, "name", "LD Radar Monitor");
    cJSON_AddStringToObject(device, "manufacturer", "ESP32-C3");
    cJSON_AddItemToObject(config, "device", device);
    json_str = cJSON_PrintUnformatted(config);
    cJSON_Delete(config);
    if (json_str) { err = app_mqtt_publish(config_topic, json_str, 0, 1, true); free(json_str); }
    
    // ---- Binary sensor: presence ----
    snprintf(config_topic, sizeof(config_topic),
             "homeassistant/binary_sensor/%s/presence/config", s_node_id);
    config = cJSON_CreateObject();
    cJSON_AddStringToObject(config, "name", "Radar Presence");
    cJSON_AddStringToObject(config, "state_topic", state_topic);
    cJSON_AddStringToObject(config, "value_template", "{{ 'ON' if value_json.target_count > 0 else 'OFF' }}");
    cJSON_AddStringToObject(config, "unique_id", s_node_id);
    cJSON_AddStringToObject(config, "availability_topic", avail_topic);
    cJSON_AddStringToObject(config, "payload_available", "online");
    cJSON_AddStringToObject(config, "payload_not_available", "offline");
    device = cJSON_CreateObject();
    cJSON_AddStringToObject(device, "identifiers", s_node_id);
    cJSON_AddStringToObject(device, "name", "LD Radar Monitor");
    cJSON_AddItemToObject(config, "device", device);
    json_str = cJSON_PrintUnformatted(config);
    cJSON_Delete(config);
    if (json_str) { err = app_mqtt_publish(config_topic, json_str, 0, 1, true); free(json_str); }
    
    // ---- Switch: LED ----
    snprintf(config_topic, sizeof(config_topic),
             "homeassistant/switch/%s/led/config", s_node_id);
    config = cJSON_CreateObject();
    cJSON_AddStringToObject(config, "name", "LED");
    cJSON_AddStringToObject(config, "state_topic", s_led_state_topic);
    cJSON_AddStringToObject(config, "command_topic", s_led_cmd_topic);
    cJSON_AddStringToObject(config, "payload_on", "ON");
    cJSON_AddStringToObject(config, "payload_off", "OFF");
    cJSON_AddStringToObject(config, "state_on", "ON");
    cJSON_AddStringToObject(config, "state_off", "OFF");
    cJSON_AddStringToObject(config, "unique_id", s_node_id);
    cJSON_AddStringToObject(config, "availability_topic", avail_topic);
    cJSON_AddStringToObject(config, "payload_available", "online");
    cJSON_AddStringToObject(config, "payload_not_available", "offline");
    device = cJSON_CreateObject();
    cJSON_AddStringToObject(device, "identifiers", s_node_id);
    cJSON_AddStringToObject(device, "name", "LD Radar Monitor");
    cJSON_AddItemToObject(config, "device", device);
    json_str = cJSON_PrintUnformatted(config);
    cJSON_Delete(config);
    if (json_str) { err = app_mqtt_publish(config_topic, json_str, 0, 1, true); free(json_str); }
    
    // Publish initial LED state
    app_mqtt_publish_led_state(gpio_control_get_led());
    
    // System info state topic
    char system_state_topic[64];
    snprintf(system_state_topic, sizeof(system_state_topic), "%s/system/state", s_node_id);
    
    // ---- Sensor: Free Heap ----
    snprintf(config_topic, sizeof(config_topic),
             "homeassistant/sensor/%s/free_heap/config", s_node_id);
    config = cJSON_CreateObject();
    cJSON_AddStringToObject(config, "name", "Free Heap");
    cJSON_AddStringToObject(config, "state_topic", system_state_topic);
    cJSON_AddStringToObject(config, "value_template", "{{ value_json.free_heap }}");
    cJSON_AddStringToObject(config, "unit_of_measurement", "B");
    cJSON_AddStringToObject(config, "device_class", "data_size");
    cJSON_AddStringToObject(config, "state_class", "measurement");
    cJSON_AddStringToObject(config, "unique_id", "free_heap");
    cJSON_AddStringToObject(config, "availability_topic", avail_topic);
    cJSON_AddStringToObject(config, "payload_available", "online");
    cJSON_AddStringToObject(config, "payload_not_available", "offline");
    device = cJSON_CreateObject();
    cJSON_AddStringToObject(device, "identifiers", s_node_id);
    cJSON_AddStringToObject(device, "name", "LD Radar Monitor");
    cJSON_AddItemToObject(config, "device", device);
    json_str = cJSON_PrintUnformatted(config);
    cJSON_Delete(config);
    if (json_str) { app_mqtt_publish(config_topic, json_str, 0, 1, true); free(json_str); }
    
    // ---- Sensor: Uptime ----
    snprintf(config_topic, sizeof(config_topic),
             "homeassistant/sensor/%s/uptime/config", s_node_id);
    config = cJSON_CreateObject();
    cJSON_AddStringToObject(config, "name", "Uptime");
    cJSON_AddStringToObject(config, "state_topic", system_state_topic);
    cJSON_AddStringToObject(config, "value_template", "{{ value_json.uptime_seconds }}");
    cJSON_AddStringToObject(config, "unit_of_measurement", "s");
    cJSON_AddStringToObject(config, "device_class", "duration");
    cJSON_AddStringToObject(config, "state_class", "total_increasing");
    cJSON_AddStringToObject(config, "unique_id", "uptime");
    cJSON_AddStringToObject(config, "availability_topic", avail_topic);
    cJSON_AddStringToObject(config, "payload_available", "online");
    cJSON_AddStringToObject(config, "payload_not_available", "offline");
    device = cJSON_CreateObject();
    cJSON_AddStringToObject(device, "identifiers", s_node_id);
    cJSON_AddStringToObject(device, "name", "LD Radar Monitor");
    cJSON_AddItemToObject(config, "device", device);
    json_str = cJSON_PrintUnformatted(config);
    cJSON_Delete(config);
    if (json_str) { app_mqtt_publish(config_topic, json_str, 0, 1, true); free(json_str); }
    
    // ---- Sensor: WiFi RSSI ----
    snprintf(config_topic, sizeof(config_topic),
             "homeassistant/sensor/%s/wifi_rssi/config", s_node_id);
    config = cJSON_CreateObject();
    cJSON_AddStringToObject(config, "name", "WiFi RSSI");
    cJSON_AddStringToObject(config, "state_topic", system_state_topic);
    cJSON_AddStringToObject(config, "value_template", "{{ value_json.wifi_rssi }}");
    cJSON_AddStringToObject(config, "unit_of_measurement", "dBm");
    cJSON_AddStringToObject(config, "device_class", "signal_strength");
    cJSON_AddStringToObject(config, "state_class", "measurement");
    cJSON_AddStringToObject(config, "unique_id", "wifi_rssi");
    cJSON_AddStringToObject(config, "availability_topic", avail_topic);
    cJSON_AddStringToObject(config, "payload_available", "online");
    cJSON_AddStringToObject(config, "payload_not_available", "offline");
    device = cJSON_CreateObject();
    cJSON_AddStringToObject(device, "identifiers", s_node_id);
    cJSON_AddStringToObject(device, "name", "LD Radar Monitor");
    cJSON_AddItemToObject(config, "device", device);
    json_str = cJSON_PrintUnformatted(config);
    cJSON_Delete(config);
    if (json_str) { app_mqtt_publish(config_topic, json_str, 0, 1, true); free(json_str); }
    
    // ---- Sensor: WiFi IP ----
    snprintf(config_topic, sizeof(config_topic),
             "homeassistant/sensor/%s/wifi_ip/config", s_node_id);
    config = cJSON_CreateObject();
    cJSON_AddStringToObject(config, "name", "WiFi IP");
    cJSON_AddStringToObject(config, "state_topic", system_state_topic);
    cJSON_AddStringToObject(config, "value_template", "{{ value_json.wifi_ip }}");
    cJSON_AddStringToObject(config, "icon", "mdi:ip-network");
    cJSON_AddStringToObject(config, "unique_id", "wifi_ip");
    cJSON_AddStringToObject(config, "availability_topic", avail_topic);
    cJSON_AddStringToObject(config, "payload_available", "online");
    cJSON_AddStringToObject(config, "payload_not_available", "offline");
    device = cJSON_CreateObject();
    cJSON_AddStringToObject(device, "identifiers", s_node_id);
    cJSON_AddStringToObject(device, "name", "LD Radar Monitor");
    cJSON_AddItemToObject(config, "device", device);
    json_str = cJSON_PrintUnformatted(config);
    cJSON_Delete(config);
    if (json_str) { app_mqtt_publish(config_topic, json_str, 0, 1, true); free(json_str); }
    
    ESP_LOGI(TAG, "HA discovery published with availability (7 entities)");
    return ESP_OK;
}

esp_err_t app_mqtt_publish_led_state(bool on)
{
    if (!app_mqtt_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }
    
    const char *state = on ? "ON" : "OFF";
    ESP_LOGI(TAG, "Publishing LED state: %s", state);
    
    return app_mqtt_publish(s_led_state_topic, state, 0, 1, true);
}