# main/CMakeLists.txt
# 主应用程序组件 CMake 配置
# 工业级重构版

# 源文件列表（仅 main.c 和 radar_adapter）
set(srcs
    "main.c"
    "radar_adapter/radar_adapter.c"
)

# 头文件目录
set(includes
    "."
)

# 注册组件
idf_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${includes}"
    REQUIRES 
        fatfs
        nvs_flash
        esp_wifi
        esp_http_server
        esp_netif
        wear_levelling
        json
        esp_timer
        mbedtls
        app_update
        web_server
)

# 链接数学库
target_link_libraries(${COMPONENT_LIB} m)
