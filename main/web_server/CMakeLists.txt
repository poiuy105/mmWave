# web_server/CMakeLists.txt
# Web 服务器组件 CMake 配置
# 工业级重构版

# 源文件列表
set(srcs
    "core/server_context.c"
    "core/http_server_core.c"
    "config/server_config.c"
    "websocket/ws_client_mgr.c"
    "websocket/ws_heartbeat.c"
    "websocket/ws_server.c"
    "security/rate_limiter.c"
    "security/input_validator.c"
    "security/security_headers.c"
    "handlers/health_handler.c"
    "http_server.c"
    "fatfs_init.c"
    "file_manager.c"
)

# 头文件目录
set(includes
    "."
    "core"
    "handlers"
    "websocket"
    "security"
    "config"
    "utils"
)

# 注册组件
idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS ${includes}
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
)

# 链接数学库
target_link_libraries(${COMPONENT_LIB} m)
