# LD2460 radar driver sources
set(LD2460_SRCS src/ld2460/radar_ld2460.c)

# LD2450 radar driver sources
set(LD2450_SRCS src/ld2450/radar_ld2450.c)

# LD6004 radar driver sources
set(LD6004_SRCS src/ld6004/radar_ld6004.c)

# LD6002B radar driver sources
set(LD6002B_SRCS src/ld6002b/radar_ld6002b.c)

# NMEA radar driver sources (original code, in main/ for now)
set(NMEA_SRCS ${CMAKE_SOURCE_DIR}/main/nmea_parser.c)

# Select sources based on configured radar type
if(CONFIG_RADAR_LD2460)
    set(RADAR_SRCS ${LD2460_SRCS})
    set(RADAR_INCLUDES include src/ld2460)
elseif(CONFIG_RADAR_LD2450)
    set(RADAR_SRCS ${LD2450_SRCS})
    set(RADAR_INCLUDES include src/ld2450)
elseif(CONFIG_RADAR_LD6004)
    set(RADAR_SRCS ${LD6004_SRCS})
    set(RADAR_INCLUDES include src/ld6004)
elseif(CONFIG_RADAR_LD6002B)
    set(RADAR_SRCS ${LD6002B_SRCS})
    set(RADAR_INCLUDES include src/ld6002b)
elseif(CONFIG_RADAR_NMEA)
    set(RADAR_SRCS ${NMEA_SRCS})
    set(RADAR_INCLUDES include ${CMAKE_SOURCE_DIR}/main)
endif()

idf_component_register(SRCS ${RADAR_SRCS}
                       INCLUDE_DIRS ${RADAR_INCLUDES}
                       REQUIRES esp_event esp_driver_uart)
