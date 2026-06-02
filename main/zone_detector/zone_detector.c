#include "zone_detector.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "zone_detector";

esp_err_t zone_detector_init(void)
{
    ESP_LOGI(TAG, "Zone detector initialized");
    return ESP_OK;
}

bool zone_detector_point_in_polygon(float x, float y, const float points[][2], uint8_t point_count)
{
    if (point_count < 3) return false;
    bool inside = false;
    for (uint8_t i = 0, j = point_count - 1; i < point_count; j = i++) {
        float xi = points[i][0], yi = points[i][1];
        float xj = points[j][0], yj = points[j][1];
        if (((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi) + xi)) {
            inside = !inside;
        }
    }
    return inside;
}

esp_err_t zone_detector_process_frame(const radar_frame_t *frame)
{
    if (!frame) return ESP_ERR_INVALID_ARG;
    zone_config_list_t list;
    esp_err_t err = zone_config_get_all(&list);
    if (err != ESP_OK) return err;
    for (uint8_t i = 0; i < list.count; i++) {
        zone_config_t *zone = &list.zones[i];
        if (!zone->enabled) continue;
        uint8_t count = 0;
        for (uint8_t t = 0; t < frame->target_count; t++) {
            const radar_target_t *target = &frame->targets[t];
            if (!target->valid) continue;
            if (zone_detector_point_in_polygon(target->x, target->y, zone->points, zone->point_count)) {
                count++;
            }
        }
        zone_config_set_target_count(zone->id, count);
        zone_config_set_triggered(zone->id, count > 0);
    }
    return ESP_OK;
}

esp_err_t zone_detector_get_results(zone_detection_results_t *results)
{
    if (!results) return ESP_ERR_INVALID_ARG;
    zone_config_list_t list;
    esp_err_t err = zone_config_get_all(&list);
    if (err != ESP_OK) return err;
    results->count = 0;
    for (uint8_t i = 0; i < list.count; i++) {
        if (list.zones[i].enabled) {
            zone_detection_result_t *r = &results->results[results->count++];
            r->zone_id = list.zones[i].id;
            r->triggered = list.zones[i].triggered;
            r->target_count = list.zones[i].target_count;
        }
    }
    return ESP_OK;
}

cJSON* zone_detector_get_status_json(void)
{
    zone_detection_results_t results;
    if (zone_detector_get_results(&results) != ESP_OK) return NULL;
    cJSON *root = cJSON_CreateArray();
    for (uint8_t i = 0; i < results.count; i++) {
        zone_detection_result_t *r = &results.results[i];
        cJSON *zone_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(zone_obj, "id", r->zone_id);
        cJSON_AddBoolToObject(zone_obj, "triggered", r->triggered);
        cJSON_AddNumberToObject(zone_obj, "target_count", r->target_count);
        cJSON_AddItemToArray(root, zone_obj);
    }
    return root;
}
