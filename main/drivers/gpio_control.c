#include "gpio_control.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "gpio_control";

// LED 状态（true=亮, false=灭）
// 注意：GPIO3 低电平 = LED 亮，高电平 = LED 灭
static bool s_led_state = false;

esp_err_t gpio_control_init(void)
{
    ESP_LOGI(TAG, "Initializing GPIO control, LED pin=%d", LED_GPIO_PIN);

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 默认 LED 灭（GPIO 高电平）
    gpio_set_level(LED_GPIO_PIN, 1);
    s_led_state = false;

    ESP_LOGI(TAG, "GPIO control initialized, LED default OFF");
    return ESP_OK;
}

esp_err_t gpio_control_set_led(bool on)
{
    // GPIO3 低电平 = LED 亮，高电平 = LED 灭
    uint32_t level = on ? 0 : 1;
    esp_err_t ret = gpio_set_level(LED_GPIO_PIN, level);
    if (ret == ESP_OK) {
        s_led_state = on;
        ESP_LOGI(TAG, "LED set to %s", on ? "ON" : "OFF");
    } else {
        ESP_LOGE(TAG, "Failed to set LED: %s", esp_err_to_name(ret));
    }
    return ret;
}

bool gpio_control_get_led(void)
{
    return s_led_state;
}

esp_err_t gpio_control_toggle_led(void)
{
    return gpio_control_set_led(!s_led_state);
}
