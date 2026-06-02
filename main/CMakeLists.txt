# main/CMakeLists.txt
# 主应用程序组件 CMake 配置
# 工业级重构版

# 源文件列表（包含所有 web_server 模块）
set(srcs
    "main.c"
    # config 模块（NVS 配置管理）
    "config/nvs_config.c"
    # mqtt 模块
    "mqtt/mqtt_client.c"
    # wifi 模块（SoftAP 配置门户）
    "wifi/wifi_manager.c"
    "wifi/softap.c"
    # core 模块（状态机）
    "core/state_machine.c"
    "radar_adapter/radar_adapter.c"
    "radar_test/radar_test.c"
    # web_server 模块
    "web_server/core/server_context.c"
    "web_server/dns_server.c"
    "web_server/core/http_server_core.c"
    "web_server/config/server_config.c"
    "web_server/websocket/ws_client_mgr.c"
    "web_server/websocket/ws_heartbeat.c"
    "web_server/websocket/ws_server.c"
    "web_server/security/rate_limiter.c"
    "web_server/security/input_validator.c"
    "web_server/security/security_headers.c"
    "web_server/handlers/health_handler.c"
    "web_server/handlers/upload_handler.c"
    "web_server/handlers/radar_handler.c"
    "web_server/http_server.c"
    "web_server/portal_handlers.c"
    "web_server/fatfs_init.c"
    "web_server/file_manager.c"
    "web_server/radar_broadcast.c"
)

# 头文件目录
set(includes
    "."
    "config"
    "mqtt"
    "wifi"
    "core"
    "radar_test"
    "web_server"
    "web_server/core"
    "web_server/handlers"
    "web_server/websocket"
    "web_server/security"
    "web_server/config"
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
        radar
        mqtt
)

# 链接数学库
target_link_libraries(${COMPONENT_LIB} m)

# 嵌入 HTML 配置页面为二进制 blob
set(html_src "web_server/portal_html.html")
set(html_bin "${CMAKE_CURRENT_BINARY_DIR}/portal_html.bin")
add_custom_command(
    OUTPUT ${html_bin}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/${html_src} ${html_bin}
    DEPENDS ${html_src}
    COMMENT "Copying portal HTML"
)
add_custom_target(portal_html DEPENDS ${html_bin})
target_add_binary_data(${COMPONENT_LIB} ${html_bin} TEXT)
