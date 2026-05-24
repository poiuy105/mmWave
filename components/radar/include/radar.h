/*
 * SPDX-FileCopyrightText: 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unified Radar Component Header
 *
 * Compile-time radar type selection via Kconfig.
 * Switching radar type only requires changing menuconfig;
 * application code using radar_config_t / radar_handle_t / radar_data_t
 * needs zero modification.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "esp_event.h"

/* ============ Compile-time radar type selection ============ */

#if defined(CONFIG_RADAR_LD2450)
    #include "radar_ld2450.h"

    typedef ld2450_config_t     radar_config_t;
    typedef ld2450_handle_t     radar_handle_t;
    typedef ld2450_event_id_t   radar_event_id_t;
    typedef ld2450_data_t       radar_data_t;

    #define RADAR_CONFIG_DEFAULT()    LD2450_CONFIG_DEFAULT()
    #define RADAR_INIT(cfg)           ld2450_init(cfg)
    #define RADAR_DEINIT(hdl)         ld2450_deinit(hdl)
    #define RADAR_ADD_HANDLER(h, fn, arg)  ld2450_add_handler(h, fn, arg)
    #define RADAR_RM_HANDLER(h, fn)       ld2450_remove_handler(h, fn)
    #define RADAR_EVENT_BASE         ESP_LD2450_EVENT
    #define RADAR_EVENT_TARGET       LD2450_EVENT_TARGET_UPDATE

#elif defined(CONFIG_RADAR_LD6002B)
    #include "radar_ld6002b.h"

    typedef ld6002b_config_t     radar_config_t;
    typedef ld6002b_handle_t     radar_handle_t;
    typedef ld6002b_event_id_t   radar_event_id_t;
    typedef ld6002b_data_t       radar_data_t;

    #define RADAR_CONFIG_DEFAULT()    LD6002B_CONFIG_DEFAULT()
    #define RADAR_INIT(cfg)           ld6002b_init(cfg)
    #define RADAR_DEINIT(hdl)         ld6002b_deinit(hdl)
    #define RADAR_ADD_HANDLER(h, fn, arg)  ld6002b_add_handler(h, fn, arg)
    #define RADAR_RM_HANDLER(h, fn)       ld6002b_remove_handler(h, fn)
    #define RADAR_EVENT_BASE         ESP_LD6002B_EVENT
    #define RADAR_EVENT_TARGET       LD6002B_EVENT_TARGET_UPDATE

#elif defined(CONFIG_RADAR_LD6004)
    #include "radar_ld6004.h"

    typedef ld6004_config_t       radar_config_t;
    typedef ld6004_handle_t       radar_handle_t;
    typedef ld6004_event_id_t     radar_event_id_t;
    typedef ld6004_data_t         radar_data_t;

    #define RADAR_CONFIG_DEFAULT()    LD6004_CONFIG_DEFAULT()
    #define RADAR_INIT(cfg)           ld6004_init(cfg)
    #define RADAR_DEINIT(hdl)         ld6004_deinit(hdl)
    #define RADAR_ADD_HANDLER(h, fn, arg)  ld6004_add_handler(h, fn, arg)
    #define RADAR_RM_HANDLER(h, fn)       ld6004_remove_handler(h, fn)
    #define RADAR_EVENT_BASE         ESP_LD6004_EVENT
    #define RADAR_EVENT_TARGET       LD6004_EVENT_TARGET_UPDATE

#elif defined(CONFIG_RADAR_LD2460)
    #include "radar_ld2460.h"

    typedef ld2460_config_t       radar_config_t;
    typedef ld2460_handle_t       radar_handle_t;
    typedef ld2460_event_id_t     radar_event_id_t;
    typedef ld2460_data_t         radar_data_t;

    #define RADAR_CONFIG_DEFAULT()    LD2460_CONFIG_DEFAULT()
    #define RADAR_INIT(cfg)           ld2460_init(cfg)
    #define RADAR_DEINIT(hdl)         ld2460_deinit(hdl)
    #define RADAR_ADD_HANDLER(h, fn, arg)  ld2460_add_handler(h, fn, arg)
    #define RADAR_RM_HANDLER(h, fn)       ld2460_remove_handler(h, fn)
    #define RADAR_EVENT_BASE         ESP_LD2460_EVENT
    #define RADAR_EVENT_TARGET       LD2460_EVENT_TARGET_UPDATE

#elif defined(CONFIG_RADAR_NMEA)
    #include "nmea_parser.h"

    typedef nmea_parser_config_t  radar_config_t;
    typedef nmea_parser_handle_t  radar_handle_t;
    typedef nmea_event_id_t       radar_event_id_t;
    typedef gps_t                 radar_data_t;

    #define RADAR_CONFIG_DEFAULT()    NMEA_PARSER_CONFIG_DEFAULT()
    #define RADAR_INIT(cfg)           nmea_parser_init(cfg)
    #define RADAR_DEINIT(hdl)         nmea_parser_deinit(hdl)
    #define RADAR_ADD_HANDLER(h, fn, arg)  nmea_parser_add_handler(h, fn, arg)
    #define RADAR_RM_HANDLER(h, fn)       nmea_parser_remove_handler(h, fn)
    #define RADAR_EVENT_BASE         ESP_NMEA_EVENT
    #define RADAR_EVENT_TARGET       GPS_UPDATE

#else
    #error "No radar type selected. Please select a radar type in menuconfig (Radar Configuration)."
#endif

#ifdef __cplusplus
}
#endif
