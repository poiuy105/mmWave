/**
 * @file zone_api_handler.c
 * @brief Zone REST API handlers for HTTP server
 *
 * Provides CRUD operations for zone configuration via REST API:
 * - GET    /api/zones      - Return all zones as JSON array
 * - POST   /api/zones      - Create a new zone
 * - PUT    /api/zones/ID   - Update a zone (ID from URI)
 * - DELETE /api/zones/ID   - Delete a zone (ID from URI)
 */

#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include "zone_config.h"

static const char *TAG = "zone_api";

// ============================================================
// Helper: send JSON response with common headers
// ============================================================

static esp_err_t send_json_response(httpd_req_t *req, cJSON *root, int status_code)
{
    httpd_resp_set_status(req, (status_code == 200) ? "200 OK" :
                              (status_code == 201) ? "201 Created" :
                              (status_code == 400) ? "400 Bad Request" :
                              (status_code == 404) ? "404 Not Found" :
                              (status_code == 500) ? "500 Internal Server Error" :
                              "200 OK");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    char *json = cJSON_PrintUnformatted(root);
    if (json == NULL) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON encode failed");
        return ESP_FAIL;
    }

    esp_err_t ret = httpd_resp_send(req, json, strlen(json));
    free(json);
    cJSON_Delete(root);
    return ret;
}

// ============================================================
// Helper: send error JSON response
// ============================================================

static esp_err_t send_error_response(httpd_req_t *req, int status_code, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "error", message);
    return send_json_response(req, root, status_code);
}

// ============================================================
// Helper: parse zone ID from URI (/api/zones/<id>)
// Returns 0 on failure (invalid ID)
// ============================================================

static uint8_t parse_zone_id_from_uri(httpd_req_t *req)
{
    const char *uri = req->uri;
    const char *prefix = "/api/zones/";

    // Find the prefix
    const char *id_str = strstr(uri, prefix);
    if (id_str == NULL) {
        return 0;
    }
    id_str += strlen(prefix);

    // Parse integer
    char *endptr;
    long id = strtol(id_str, &endptr, 10);
    if (endptr == id_str || *endptr != '\0' || id < 1 || id > 255) {
        return 0;
    }

    return (uint8_t)id;
}

// ============================================================
// Helper: validate zone data from JSON
// Returns true if valid, false otherwise
// ============================================================

static bool validate_zone_json(cJSON *zone_obj)
{
    cJSON *name = cJSON_GetObjectItem(zone_obj, "name");
    cJSON *point_count = cJSON_GetObjectItem(zone_obj, "point_count");
    cJSON *points = cJSON_GetObjectItem(zone_obj, "points");

    // name is optional, but if present must be a string
    if (name && !cJSON_IsString(name)) {
        ESP_LOGW(TAG, "Invalid name field");
        return false;
    }
    if (name && strlen(name->valuestring) >= ZONE_NAME_MAX_LEN) {
        ESP_LOGW(TAG, "Name too long");
        return false;
    }

    // point_count is required, must be 3-10
    if (!cJSON_IsNumber(point_count)) {
        ESP_LOGW(TAG, "Missing or invalid point_count");
        return false;
    }
    int pc = point_count->valueint;
    if (pc < 3 || pc > 10) {
        ESP_LOGW(TAG, "point_count out of range: %d", pc);
        return false;
    }

    // points is required, must be array with point_count elements
    if (!cJSON_IsArray(points)) {
        ESP_LOGW(TAG, "Missing or invalid points array");
        return false;
    }
    if (cJSON_GetArraySize(points) < pc) {
        ESP_LOGW(TAG, "Not enough points: got %d, expected %d",
                 cJSON_GetArraySize(points), pc);
        return false;
    }

    // Validate each point: [x, y] with range +/- 20m
    for (int i = 0; i < pc; i++) {
        cJSON *pt = cJSON_GetArrayItem(points, i);
        if (!cJSON_IsArray(pt) || cJSON_GetArraySize(pt) < 2) {
            ESP_LOGW(TAG, "Invalid point[%d]: not an array or too short", i);
            return false;
        }
        cJSON *x = cJSON_GetArrayItem(pt, 0);
        cJSON *y = cJSON_GetArrayItem(pt, 1);
        if (!cJSON_IsNumber(x) || !cJSON_IsNumber(y)) {
            ESP_LOGW(TAG, "Invalid point[%d]: x or y not a number", i);
            return false;
        }
        if (x->valuedouble < -20.0f || x->valuedouble > 20.0f ||
            y->valuedouble < -20.0f || y->valuedouble > 20.0f) {
            ESP_LOGW(TAG, "Invalid point[%d]: coordinate out of range (%.2f, %.2f)",
                     i, x->valuedouble, y->valuedouble);
            return false;
        }
    }

    return true;
}

// ============================================================
// Helper: build zone JSON object from zone_config_t
// ============================================================

static cJSON* zone_to_json(const zone_config_t *zone)
{
    cJSON *obj = cJSON_CreateObject();
    if (obj == NULL) return NULL;

    cJSON_AddNumberToObject(obj, "id", zone->id);
    cJSON_AddStringToObject(obj, "name", zone->name);
    cJSON_AddNumberToObject(obj, "point_count", zone->point_count);
    cJSON_AddBoolToObject(obj, "enabled", zone->enabled);
    cJSON_AddStringToObject(obj, "color", zone->color);
    cJSON_AddBoolToObject(obj, "triggered", zone->triggered);
    cJSON_AddNumberToObject(obj, "target_count", zone->target_count);

    cJSON *points_arr = cJSON_CreateArray();
    if (points_arr) {
        for (uint8_t i = 0; i < zone->point_count; i++) {
            cJSON *pt = cJSON_CreateArray();
            if (pt) {
                cJSON_AddItemToArray(pt, cJSON_CreateNumber(zone->points[i][0]));
                cJSON_AddItemToArray(pt, cJSON_CreateNumber(zone->points[i][1]));
                cJSON_AddItemToArray(points_arr, pt);
            }
        }
        cJSON_AddItemToObject(obj, "points", points_arr);
    }

    return obj;
}

// ============================================================
// Helper: parse zone_config_t from JSON object
// ============================================================

static bool json_to_zone(cJSON *zone_obj, zone_config_t *zone)
{
    memset(zone, 0, sizeof(zone_config_t));

    cJSON *name = cJSON_GetObjectItem(zone_obj, "name");
    cJSON *point_count = cJSON_GetObjectItem(zone_obj, "point_count");
    cJSON *points = cJSON_GetObjectItem(zone_obj, "points");
    cJSON *enabled = cJSON_GetObjectItem(zone_obj, "enabled");
    cJSON *color = cJSON_GetObjectItem(zone_obj, "color");

    if (name && cJSON_IsString(name)) {
        strncpy(zone->name, name->valuestring, ZONE_NAME_MAX_LEN - 1);
    } else {
        strncpy(zone->name, "unnamed", ZONE_NAME_MAX_LEN - 1);
    }

    zone->point_count = (uint8_t)point_count->valueint;

    for (uint8_t i = 0; i < zone->point_count; i++) {
        cJSON *pt = cJSON_GetArrayItem(points, i);
        if (pt && cJSON_IsArray(pt) && cJSON_GetArraySize(pt) >= 2) {
            zone->points[i][0] = (float)cJSON_GetArrayItem(pt, 0)->valuedouble;
            zone->points[i][1] = (float)cJSON_GetArrayItem(pt, 1)->valuedouble;
        }
    }

    if (enabled && cJSON_IsBool(enabled)) {
        zone->enabled = cJSON_IsTrue(enabled);
    } else {
        zone->enabled = true;  // default enabled
    }

    if (color && cJSON_IsString(color)) {
        snprintf(zone->color, sizeof(zone->color), "%s", color->valuestring);
    } else {
        snprintf(zone->color, sizeof(zone->color), "%s", "#00ff00");
    }

    return true;
}

// ============================================================
// GET /api/zones - Return all zones as JSON array
// ============================================================

esp_err_t zone_api_get_all_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "GET /api/zones");

    zone_config_list_t list;
    esp_err_t err = zone_config_get_all(&list);
    if (err != ESP_OK) {
        return send_error_response(req, 500, "Failed to read zone config");
    }

    cJSON *arr = cJSON_CreateArray();
    if (arr == NULL) {
        return send_error_response(req, 500, "Memory allocation failed");
    }

    for (uint8_t i = 0; i < list.count; i++) {
        cJSON *zone_obj = zone_to_json(&list.zones[i]);
        if (zone_obj) {
            cJSON_AddItemToArray(arr, zone_obj);
        }
    }

    return send_json_response(req, arr, 200);
}

// ============================================================
// POST /api/zones - Create a new zone
// ============================================================

esp_err_t zone_api_create_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/zones");

    // Read request body
    char buf[1024];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        return send_error_response(req, 400, "Empty request body");
    }
    buf[len] = '\0';

    // Parse JSON
    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        ESP_LOGW(TAG, "JSON parse failed: %s", buf);
        return send_error_response(req, 400, "Invalid JSON");
    }

    // Validate
    if (!validate_zone_json(root)) {
        cJSON_Delete(root);
        return send_error_response(req, 400, "Validation failed: check point_count (3-10) and coordinates (±20m)");
    }

    // Parse into zone_config_t
    zone_config_t zone;
    json_to_zone(root, &zone);
    cJSON_Delete(root);

    // Add zone
    esp_err_t err = zone_config_add(&zone);
    if (err == ESP_ERR_NO_MEM) {
        return send_error_response(req, 400, "Maximum zone count reached");
    }
    if (err != ESP_OK) {
        return send_error_response(req, 500, "Failed to add zone");
    }

    // Save to NVS
    err = zone_config_save();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save zone config to NVS");
    }

    // Return the created zone (re-read to get assigned ID)
    zone_config_list_t list;
    zone_config_get_all(&list);
    zone_config_t *created = &list.zones[list.count - 1];

    cJSON *resp = zone_to_json(created);
    if (resp == NULL) {
        return send_error_response(req, 500, "Failed to build response");
    }

    return send_json_response(req, resp, 201);
}

// ============================================================
// PUT /api/zones/* - Update a zone
// ============================================================

esp_err_t zone_api_update_handler(httpd_req_t *req)
{
    uint8_t id = parse_zone_id_from_uri(req);
    if (id == 0) {
        return send_error_response(req, 400, "Invalid zone ID");
    }

    ESP_LOGI(TAG, "PUT /api/zones/%d", id);

    // Check zone exists
    zone_config_t *existing = zone_config_get(id);
    if (existing == NULL) {
        return send_error_response(req, 404, "Zone not found");
    }

    // Read request body
    char buf[1024];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        return send_error_response(req, 400, "Empty request body");
    }
    buf[len] = '\0';

    // Parse JSON
    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        ESP_LOGW(TAG, "JSON parse failed: %s", buf);
        return send_error_response(req, 400, "Invalid JSON");
    }

    // Validate
    if (!validate_zone_json(root)) {
        cJSON_Delete(root);
        return send_error_response(req, 400, "Validation failed: check point_count (3-10) and coordinates (±20m)");
    }

    // Parse into zone_config_t
    zone_config_t zone;
    json_to_zone(root, &zone);
    cJSON_Delete(root);

    // Update zone (preserves runtime state)
    esp_err_t err = zone_config_update(id, &zone);
    if (err != ESP_OK) {
        return send_error_response(req, 500, "Failed to update zone");
    }

    // Save to NVS
    err = zone_config_save();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save zone config to NVS");
    }

    // Return updated zone
    zone_config_t *updated = zone_config_get(id);
    cJSON *resp = zone_to_json(updated);
    if (resp == NULL) {
        return send_error_response(req, 500, "Failed to build response");
    }

    return send_json_response(req, resp, 200);
}

// ============================================================
// DELETE /api/zones/* - Delete a zone
// ============================================================

esp_err_t zone_api_delete_handler(httpd_req_t *req)
{
    uint8_t id = parse_zone_id_from_uri(req);
    if (id == 0) {
        return send_error_response(req, 400, "Invalid zone ID");
    }

    ESP_LOGI(TAG, "DELETE /api/zones/%d", id);

    // Delete zone
    esp_err_t err = zone_config_delete(id);
    if (err == ESP_ERR_NOT_FOUND) {
        return send_error_response(req, 404, "Zone not found");
    }
    if (err != ESP_OK) {
        return send_error_response(req, 500, "Failed to delete zone");
    }

    // Save to NVS
    err = zone_config_save();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save zone config to NVS");
    }

    // Return success
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "message", "Zone deleted");
    cJSON_AddNumberToObject(resp, "id", id);
    return send_json_response(req, resp, 200);
}
