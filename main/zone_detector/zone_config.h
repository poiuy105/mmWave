#ifndef ZONE_CONFIG_H
#define ZONE_CONFIG_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#define ZONE_MAX_COUNT      8
#define ZONE_MAX_POINTS     10
#define ZONE_NAME_MAX_LEN   32
#define ZONE_COLOR_MAX_LEN  8

#define NVS_NAMESPACE_ZONES "zone_config"
#define NVS_KEY_ZONE_COUNT  "zone_count"

typedef struct {
    uint8_t id;
    char name[ZONE_NAME_MAX_LEN];
    uint8_t point_count;
    float points[ZONE_MAX_POINTS][2];
    char color[ZONE_COLOR_MAX_LEN];
    bool enabled;
    bool triggered;
    uint8_t target_count;
} zone_config_t;

typedef struct {
    uint8_t count;
    zone_config_t zones[ZONE_MAX_COUNT];
} zone_config_list_t;

esp_err_t zone_config_init(void);
void zone_config_deinit(void);
esp_err_t zone_config_get_all(zone_config_list_t *list);
zone_config_t* zone_config_get(uint8_t id);
esp_err_t zone_config_add(const zone_config_t *zone);
esp_err_t zone_config_update(uint8_t id, const zone_config_t *zone);
esp_err_t zone_config_delete(uint8_t id);
esp_err_t zone_config_save(void);
esp_err_t zone_config_set_triggered(uint8_t id, bool triggered);
esp_err_t zone_config_set_target_count(uint8_t id, uint8_t count);

#endif
