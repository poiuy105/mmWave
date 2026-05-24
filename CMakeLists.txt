# LD Radar Web Monitor - CMakeLists.txt
# ESP32-C3 嵌入式 Web 服务器监控系统

cmake_minimum_required(VERSION 3.5)

# 包含项目
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# 项目名称
project(ld_radar_monitor)

# 设置分区表
set(PARTITION_TABLE_CUSTOM "partitions.csv")
set(PARTITION_TABLE_CUSTOM_FILENAME "partitions.csv")
