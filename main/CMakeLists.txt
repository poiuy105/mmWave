# main/CMakeLists.txt

# 源文件列表
set(srcs
    "main.c"
    "web_server/fatfs_init.c"
    "web_server/http_server.c"
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
)

# 嵌入网页文件到 FATFS (可选，用于首次烧录时初始化)
# 这些文件会在 fatfs_init 时复制到 FATFS 分区
set(WEB_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/../storage/www/index.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/../storage/www/css/main.css"
    "${CMAKE_CURRENT_SOURCE_DIR}/../storage/www/css/layout.css"
    "${CMAKE_CURRENT_SOURCE_DIR}/../storage/www/css/canvas.css"
    "${CMAKE_CURRENT_SOURCE_DIR}/../storage/www/js/utils.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/../storage/www/js/config.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/../storage/www/js/api.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/../storage/www/js/websocket.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/../storage/www/js/radar-canvas.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/../storage/www/js/app.js"
)

# 创建存储目录结构
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../storage/www/css)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../storage/www/js)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../storage/logs)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../storage/config)
