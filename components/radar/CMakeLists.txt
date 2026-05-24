# LD2460 radar driver sources
set(LD2460_SRCS src/ld2460/radar_ld2460.c)

# NMEA radar driver sources (original code, in main/ for now)
set(NMEA_SRCS ${CMAKE_SOURCE_DIR}/main/nmea_parser.c)

# Select sources based on configured radar type
if(CONFIG_RADAR_LD2460)
    set(RADAR_SRCS ${LD2460_SRCS})
    set(RADAR_INCLUDES include src/ld2460)
elseif(CONFIG_RADAR_NMEA)
    set(RADAR_SRCS ${NMEA_SRCS})
    set(RADAR_INCLUDES include ${CMAKE_SOURCE_DIR}/main)
endif()

idf_component_register(SRCS ${RADAR_SRCS}
                       INCLUDE_DIRS ${RADAR_INCLUDES}
                       REQUIRES esp_event esp_driver_uart)
