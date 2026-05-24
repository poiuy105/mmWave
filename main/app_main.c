/*
 * SPDX-FileCopyrightText: 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * LD2460 Radar Demo Application
 *
 * UART1 → LD2460 radar
 * UART0 → PC serial monitor (printf)
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "radar.h"

static const char *TAG = "APP";

#if defined(CONFIG_RADAR_LD2460)

static void radar_event_handler(void *handler_args, esp_event_base_t base,
                                 int32_t event_id, void *event_data)
{
    ld2460_data_t *data = (ld2460_data_t *)event_data;

    if (data->target_count == 0) {
        printf("No target detected\n");
        return;
    }

    printf("Targets: %d\n", data->target_count);
    for (int i = 0; i < data->target_count; i++) {
        printf("  [%d] X=%.1f m, Y=%.1f m\n",
               i,
               data->targets[i].x * 0.1f,
               data->targets[i].y * 0.1f);
    }
}

#elif defined(CONFIG_RADAR_NMEA)

static void radar_event_handler(void *handler_args, esp_event_base_t base,
                                 int32_t event_id, void *event_data)
{
    gps_t *gps = (gps_t *)event_data;
    printf("GPS: lat=%.6f lon=%.6f sats=%d\n",
           gps->latitude, gps->longitude, gps->sats_in_use);
}

#endif

void app_main(void)
{
    ESP_LOGI(TAG, "Radar demo starting...");

    /* Initialize radar with default config (from Kconfig) */
    radar_config_t config = RADAR_CONFIG_DEFAULT();
    radar_handle_t radar = RADAR_INIT(&config);
    if (radar == NULL) {
        ESP_LOGE(TAG, "Failed to initialize radar");
        return;
    }

    /* Register event handler */
    RADAR_ADD_HANDLER(radar, radar_event_handler, NULL);

#if defined(CONFIG_RADAR_LD2460)
    /* LD2460 specific: query and print firmware version */
    ld2460_version_t version;
    if (ld2460_get_firmware_version(radar, &version) == ESP_OK) {
        ESP_LOGI(TAG, "LD2460 firmware: V%d.%d (%02d/%02d) mode=%s",
                 version.version_major, version.version_minor,
                 version.year, version.month,
                 version.mode == LD2460_INSTALL_SIDE ? "side" : "top");
    }

    /* LD2460 specific: query installation mode */
    ld2460_install_mode_t mode;
    if (ld2460_get_install_mode(radar, &mode) == ESP_OK) {
        ESP_LOGI(TAG, "Install mode: %s", mode == LD2460_INSTALL_SIDE ? "side" : "top");
    }

    /* LD2460 specific: query detection range */
    ld2460_range_t range;
    if (ld2460_get_detection_range(radar, &range) == ESP_OK) {
        ESP_LOGI(TAG, "Detection range: %.1f m, angle %.1f° ~ %.1f°",
                 range.distance, range.angle_start, range.angle_end);
    }
#endif

    ESP_LOGI(TAG, "Radar initialized, waiting for data...");

    /* Main loop: just sleep, events are handled by radar task */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
