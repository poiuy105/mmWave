# 所有雷达驱动源文件
set(RADAR_SRCS
    src/ld2460/radar_ld2460.c
    src/ld2450/radar_ld2450.c
    src/ld6004/radar_ld6004.c
    src/ld6002b/radar_ld6002b.c
    src/ld2461/radar_ld2461.c
    src/ld2452/radar_ld2452.c
    src/r60abd1/radar_r60abd1.c
)

# 所有头文件目录
set(RADAR_INCLUDES
    include
    src/ld2460
    src/ld2450
    src/ld6004
    src/ld6002b
    src/ld2461
    src/ld2452
    src/r60abd1
)

idf_component_register(SRCS ${RADAR_SRCS}
                       INCLUDE_DIRS ${RADAR_INCLUDES}
                       REQUIRES esp_event esp_driver_uart)
