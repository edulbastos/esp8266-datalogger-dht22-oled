cmake_minimum_required(VERSION 3.5)

# Suprimir todos os warnings de desenvolvimento do CMake
set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS 1 CACHE BOOL "Suppress developer warnings" FORCE)
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "Suppress deprecated warnings" FORCE)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp8266_datalogger_oled_ssd1306v2.0)