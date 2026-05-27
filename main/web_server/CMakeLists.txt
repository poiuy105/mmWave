# web_server/CMakeLists.txt
# Web 服务器组件 CMake 配置
# 工业级重构版

idf_component_register(
    SRCS
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
        "http_server_new.c"
    INCLUDE_DIRS
        "."
        "core"
        "handlers"
        "websocket"
        "security"
        "config"
        "utils"
)
