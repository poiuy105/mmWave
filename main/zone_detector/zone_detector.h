#ifndef ZONE_DETECTOR_H
#define ZONE_DETECTOR_H

#include "esp_err.h"
#include "radar_adapter/radar_adapter.h"
#include "zone_config.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t zone_id;
    bool triggered;
    uint8_t target_count;
} zone_detection_result_t;

typedef struct {
    uint8_t count;
    zone_detection_result_t results[ZONE_MAX_COUNT];
} zone_detection_results_t;

esp_err_t zone_detector_init(void);
bool zone_detector_point_in_polygon(float x, float y, const float points[][2], uint8_t point_count);
esp_err_t zone_detector_process_frame(const radar_frame_t *frame);
esp_err_t zone_detector_get_results(zone_detection_results_t *results);
cJSON* zone_detector_get_status_json(void);

#endif
