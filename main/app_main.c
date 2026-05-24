/*
 * SPDX-FileCopyrightText: 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Radar Demo Application
 *
 * Supports: HLK-LD2461, HLK-LD2450, HLK-LD6002B, HLK-LD6004, HLK-LD2460, NMEA
 * UART1 → Radar
 * UART0 → PC serial monitor (printf)
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "radar.h"

static const char *TAG = "APP";

#if defined(CONFIG_RADAR_LD2461)

static void radar_event_handler(void *handler_args, esp_event_base_t base,
                                 int32_t event_id, void *event_data)
{
    if (event_id == LD2461_EVENT_COORDINATES) {
        ld2461_data_t *data = (ld2461_data_t *)event_data;
        if (data->target_count == 0) {
            printf("No target detected\n");
            return;
        }
        printf("Targets: %d\n", data->target_count);
        for (int i = 0; i < data->target_count; i++) {
            printf("  [%d] X=%.1f m, Y=%.1f m\n",
                   i,
                   data->targets[i].x,
                   data->targets[i].y);
        }
    } else if (event_id == LD2461_EVENT_REGION_STATUS) {
        ld2461_region_status_t *status = (ld2461_region_status_t *)event_data;
        printf("Region status: [R1:%s, R2:%s, R3:%s]\n",
               status->region1_occupied ? "OCC" : "EMP",
               status->region2_occupied ? "OCC" : "EMP",
               status->region3_occupied ? "OCC" : "EMP");
    }
}

#elif defined(CONFIG_RADAR_LD2450)

static void radar_event_handler(void *handler_args, esp_event_base_t base,
                                 int32_t event_id, void *event_data)
{
    ld2450_data_t *data = (ld2450_data_t *)event_data;
    if (data->target_count == 0) {
        printf("No target detected\n");
        return;
    }
    printf("Targets: %d\n", data->target_count);
    for (int i = 0; i < data->target_count; i++) {
        printf("  [%d] X=%d mm, Y=%d mm, speed=%d cm/s, res=%d mm\n",
               i,
               data->targets[i].x,
               data->targets[i].y,
               data->targets[i].speed,
               data->targets[i].resolution);
    }
}

#elif defined(CONFIG_RADAR_LD6002B)

static void radar_event_handler(void *handler_args, esp_event_base_t base,
                                 int32_t event_id, void *event_data)
{
    if (event_id == LD6002B_EVENT_TARGET_UPDATE) {
        ld6002b_data_t *data = (ld6002b_data_t *)event_data;
        if (data->target_count == 0) {
            printf("No target detected\n");
            return;
        }
        printf("Targets: %d\n", data->target_count);
        for (int i = 0; i < data->target_count; i++) {
            printf("  [%d] X=%.2f Y=%.2f Z=%.2f dop=%ld cluster=%ld\n",
                   i,
                   data->targets[i].x,
                   data->targets[i].y,
                   data->targets[i].z,
                   (long)data->targets[i].dop_idx,
                   (long)data->targets[i].cluster_id);
        }
    } else if (event_id == LD6002B_EVENT_AREA_STATE) {
        ld6002b_area_state_t *state = (ld6002b_area_state_t *)event_data;
        printf("Area state: [%d,%d,%d,%d]\n",
               state->states[0], state->states[1],
               state->states[2], state->states[3]);
    }
}

#elif defined(CONFIG_RADAR_LD6004)

static void radar_event_handler(void *handler_args, esp_event_base_t base,
                                 int32_t event_id, void *event_data)
{
    if (event_id == LD6004_EVENT_TARGET_UPDATE) {
        ld6004_data_t *data = (ld6004_data_t *)event_data;
        if (data->target_count == 0) {
            printf("No target detected\n");
            return;
        }
        printf("Targets: %d\n", data->target_count);
        for (int i = 0; i < data->target_count; i++) {
            printf("  [%d] X=%.2f Y=%.2f Z=%.2f dop=%ld cluster=%ld\n",
                   i,
                   data->targets[i].x,
                   data->targets[i].y,
                   data->targets[i].z,
                   (long)data->targets[i].dop_idx,
                   (long)data->targets[i].cluster_id);
        }
    } else if (event_id == LD6004_EVENT_AREA_STATE) {
        ld6004_area_state_t *state = (ld6004_area_state_t *)event_data;
        printf("Area state: [%d,%d,%d,%d]\n",
               state->states[0], state->states[1],
               state->states[2], state->states[3]);
    }
}

#elif defined(CONFIG_RADAR_LD2460)

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

#if defined(CONFIG_RADAR_LD2450)
    /* LD2450 specific: query firmware version */
    ld2450_version_t ver;
    if (ld2450_get_firmware_version(radar, &ver) == ESP_OK) {
        ESP_LOGI(TAG, "LD2450 firmware: V%lu.%lu", (unsigned long)ver.major, (unsigned long)ver.minor);
    }

    /* LD2450 specific: query tracking mode */
    ld2450_tracking_mode_t mode;
    if (ld2450_get_tracking_mode(radar, &mode) == ESP_OK) {
        ESP_LOGI(TAG, "Tracking mode: %s", mode == LD2450_TRACKING_SINGLE ? "single" : "multi");
    }

#elif defined(CONFIG_RADAR_LD6002B)
    /* LD6002B specific: query install mode */
    ld6002b_install_mode_t mode;
    if (ld6002b_get_install_mode(radar, &mode) == ESP_OK) {
        ESP_LOGI(TAG, "Install mode: %s", mode == LD6002B_INSTALL_TOP ? "top" : "side");
    }

    /* LD6002B specific: query sensitivity */
    ld6002b_sensitivity_t sens;
    if (ld6002b_get_sensitivity(radar, &sens) == ESP_OK) {
        ESP_LOGI(TAG, "Sensitivity: %d", sens);
    }

#elif defined(CONFIG_RADAR_LD6004)
    /* LD6004 specific: query firmware version */
    if (ld6004_query_firmware_version(radar) == ESP_OK) {
        ESP_LOGI(TAG, "LD6004 firmware queried successfully");
    }

    /* LD6004 specific: query install mode */
    ld6004_install_mode_t mode;
    if (ld6004_get_install_mode(radar, &mode) == ESP_OK) {
        ESP_LOGI(TAG, "Install mode: %s", mode == LD6004_INSTALL_TOP ? "top" : "side");
    }

    /* LD6004 specific: query work mode */
    ld6004_work_mode_t wmode;
    if (ld6004_get_work_mode(radar, &wmode) == ESP_OK) {
        ESP_LOGI(TAG, "Work mode: %d", wmode);
    }

#elif defined(CONFIG_RADAR_LD2460)
    ld2460_version_t version;
    if (ld2460_get_firmware_version(radar, &version) == ESP_OK) {
        ESP_LOGI(TAG, "LD2460 firmware: V%d.%d (%02d/%02d) mode=%s",
                 version.version_major, version.version_minor,
                 version.year, version.month,
                 version.mode == LD2460_INSTALL_SIDE ? "side" : "top");
    }

#elif defined(CONFIG_RADAR_LD2461)
    /* LD2461 specific: query version */
    ld2461_version_t ver;
    if (ld2461_get_version(radar, &ver) == ESP_OK) {
        ESP_LOGI(TAG, "LD2461 firmware: V%d.%d (20%02d/%02d/%02d) ID=0x%08lX",
                 ver.major, ver.minor,
                 ver.year, ver.month, ver.day,
                 (unsigned long)ver.unique_id);
    }

    /* LD2461 specific: query report format */
    ld2461_report_format_t fmt;
    if (ld2461_get_report_format(radar, &fmt) == ESP_OK) {
        const char *fmt_str = (fmt == LD2461_REPORT_COORDINATES) ? "coordinates" :
                              (fmt == LD2461_REPORT_STATUS) ? "status" : "both";
        ESP_LOGI(TAG, "Report format: %s", fmt_str);
    }
#endif

    ESP_LOGI(TAG, "Radar initialized, waiting for data...");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
