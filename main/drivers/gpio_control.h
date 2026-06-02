#ifndef GPIO_CONTROL_H
#define GPIO_CONTROL_H

#include "esp_err.h"
#include <stdbool.h>

// LED GPIO 定义
#define LED_GPIO_PIN    3

/**
 * @brief 初始化 GPIO 控制
 * @return ESP_OK 成功
 */
esp_err_t gpio_control_init(void);

/**
 * @brief 设置 LED 状态
 * @param on true=亮, false=灭
 * @return ESP_OK 成功
 */
esp_err_t gpio_control_set_led(bool on);

/**
 * @brief 获取 LED 当前状态
 * @return true=亮, false=灭
 */
bool gpio_control_get_led(void);

/**
 * @brief 切换 LED 状态
 * @return ESP_OK 成功
 */
esp_err_t gpio_control_toggle_led(void);

#endif
