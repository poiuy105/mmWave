# main/CMakeLists.txt
# 主应用程序 CMake 配置
# 工业级重构版

# 源文件列表
set(srcs
    "main.c"
    "web_server/fatfs_init.c"
    # "web_server/http_server.c"      # 旧版，已由新模块替代
    # "web_server/websocket_server.c" # 旧版，已由新模块替代
    "web_server/file_manager.c"
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
        radar
        esp_timer
        mbedtls
        app_update
)

# 链接数学库
target_link_libraries(${COMPONENT_LIB} m)

# ============================================================
# FATFS 镜像生成配置（可选）
# 使用 fatfs_create_partition_image 将前端文件打包进固件
# ============================================================

# 前端文件目录（相对于项目根目录）
set(FRONTEND_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../frontend")

# 检查前端目录是否存在
if(EXISTS "${FRONTEND_DIR}")
    message(STATUS "Frontend directory found: ${FRONTEND_DIR}")

    # 创建存储目录结构（编译时创建）
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../storage/www/css)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../storage/www/js)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../storage/logs)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../storage/config)

    # 如果 frontend/www 目录存在，创建 FATFS 镜像
    # 注意：这需要分区表类型为 fat，而非 spiffs
    # 
    # 使用方法：
    # 1. 修改 partitions.csv 中的 storage 分区类型为 fat
    # 2. 取消下面的注释
    #
    # if(EXISTS "${FRONTEND_DIR}/www")
    #     message(STATUS "Creating FATFS partition image from ${FRONTEND_DIR}/www")
    #     fatfs_create_partition_image(
    #         "storage"
    #         "${FRONTEND_DIR}/www"
    #         FLASH_IN_PROJECT
    #     )
    # endif()

else()
    message(STATUS "Frontend directory not found: ${FRONTEND_DIR}")
endif()

# ============================================================
# 固件版本信息
# ============================================================

# 从 Git 获取版本信息（如果没有 Git，使用默认值）
execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

if(NOT GIT_VERSION)
    set(GIT_VERSION "v2.0.0")
endif()

message(STATUS "Firmware version: ${GIT_VERSION}")

# 定义版本宏
add_definitions(-DGIT_VERSION=\"${GIT_VERSION}\")
add_definitions(-DWEB_SERVER_VERSION=\"2.0.0\")
add_definitions(-DWEB_SERVER_BUILD_DATE=\"${CMAKE_CURRENT_TIME}\")
