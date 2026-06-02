#include "state_machine.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "state_machine";
static state_machine_t s_machine = {0};

static const char* state_to_str(app_state_t state)
{
    switch (state) {
        case STATE_INIT: return "INIT";
        case STATE_SOFTAP_CONFIG: return "SOFTAP_CONFIG";
        case STATE_WIFI_CONNECTING: return "WIFI_CONNECTING";
        case STATE_WIFI_CONNECTED: return "WIFI_CONNECTED";
        case STATE_MQTT_CONNECTING: return "MQTT_CONNECTING";
        case STATE_MQTT_CONNECTED: return "MQTT_CONNECTED";
        case STATE_RUNNING: return "RUNNING";
        case STATE_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

esp_err_t state_machine_init(void)
{
    s_machine.state = STATE_INIT;
    s_machine.state_enter_time = xTaskGetTickCount();
    s_machine.retry_count = 0;
    ESP_LOGI(TAG, "State machine initialized");
    return ESP_OK;
}

app_state_t state_machine_get_state(void)
{
    return s_machine.state;
}

void state_machine_transition(app_state_t new_state)
{
    if (s_machine.state != new_state) {
        ESP_LOGI(TAG, "State transition: %s -> %s", 
                 state_to_str(s_machine.state), state_to_str(new_state));
        s_machine.state = new_state;
        s_machine.state_enter_time = xTaskGetTickCount();
        if (new_state == STATE_INIT || new_state == STATE_WIFI_CONNECTING || 
            new_state == STATE_MQTT_CONNECTING) {
            s_machine.retry_count = 0;
        }
    }
}

void state_machine_run(void)
{
    // Simplified state machine - actual logic handled in main.c
    // This can be expanded for more complex state management
}
