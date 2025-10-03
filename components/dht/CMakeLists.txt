idf_component_register(SRCS "dht.c"
                    INCLUDE_DIRS "."
                    REQUIRES "esp8266" "freertos" "log" "esp_idf_lib_helpers")