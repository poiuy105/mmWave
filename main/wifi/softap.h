#ifndef SOFTAP_H
#define SOFTAP_H

#include "esp_err.h"
#include <stddef.h>

#define DEFAULT_SOFTAP_SSID "LD-Radar-AP"
#define DEFAULT_SOFTAP_CHANNEL 1

/**
 * @brief 生成带 MAC 后缀的 SoftAP SSID
 * @param ssid_out 输出缓冲区
 * @param ssid_size 缓冲区大小
 * @return ESP_OK 成功
 */
esp_err_t softap_generate_ssid_with_mac(char *ssid_out, size_t ssid_size);

#endif
