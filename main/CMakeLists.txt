# CMakeLists.txt atualizado para o projeto refatorado

set(COMPONENT_REQUIRES esp8266 freertos dht esp_idf_lib_helpers ssd1306)

# Lista de todos os arquivos source do projeto refatorado
set(COMPONENT_SRCS 
    "main.c"
    "globals.c"
    "spiffs_manager.c"
    "dns_manager.c"
    "measurement.c"
    "ntp_manager.c"
    "time_cache.c"
    "mqtt_manager.c"
    "wifi_manager.c"
    "http_server.c"
    "oled_display.c"
    "system_status.c"
)

idf_component_register(
    SRCS ${COMPONENT_SRCS}
    INCLUDE_DIRS "."
)

target_link_libraries(${COMPONENT_LIB} "-u _printf_float")
