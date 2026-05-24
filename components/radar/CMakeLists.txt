# LD2460 radar driver sources
set(LD2460_SRCS src/ld2460/radar_ld2460.c)

# NMEA radar driver sources (original code)
set(NMEA_SRCS src/nmea/nmea_parser.c)

# Select sources based on configured radar type
if(CONFIG_RADAR_LD2460)
    set(RADAR_SRCS ${LD2460_SRCS})
elseif(CONFIG_RADAR_NMEA)
    set(RADAR_SRCS ${NMEA_SRCS})
endif()

idf_component_register(SRCS ${RADAR_SRCS}
                       INCLUDE_DIRS include src/ld2460 src/nmea
                       REQUIRES esp_event esp_driver_uart)
