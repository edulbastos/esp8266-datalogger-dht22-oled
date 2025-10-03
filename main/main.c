// Projeto Completo - ESP8266 Datalogger com SPIFFS Backup
// ESP8266_RTOS_SDK com FreeRTOS, WiFi, NTP, MQTT e SPIFFS
// Versão Refatorada - Código modularizado para melhor manutenibilidade

#include <stdio.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "ssd1306.h"

// Includes dos módulos refatorados
#include "config.h"
#include "types.h"
#include "globals.h"
#include "spiffs_manager.h"
#include "dns_manager.h"
#include "measurement.h"
#include "ntp_manager.h"
#include "mqtt_manager.h"
#include "wifi_manager.h"
#include "http_server.h"
#include "oled_display.h"
#include "system_status.h"

// Task configuration macros
#define TASK_STACK_SMALL 2048
#define TASK_STACK_MED   4096
#define TASK_STACK_LARGE 8192

#define PRIO_OLED        7
#define PRIO_WIFI        6
#define PRIO_NTP         5
#define PRIO_MQTT_MON    4
#define PRIO_MEASUREMENT 3
#define PRIO_HTTP        2
#define PRIO_SYS_STATUS  1


// Inicialização do sistema: NVS, SPIFFS, Event Groups, Queues, Mutexes
static esp_err_t init_system(void) {
    ESP_LOGI(TAG, "System init: %s", FIRMWARE_VERSION);

    // Inicializar display OLED
    ssd1306_128x64_i2c_initEx(I2C_MASTER_SCL_IO, I2C_MASTER_SDA_IO, I2C_OLED_ADDR);
#ifdef CONFIG_ENABLE_OLED_DISPLAY
    ssd1306_clearScreen();
    ESP_LOGI(TAG, "OLED display initialized successfully");
#else
    // Limpar tela e desligar display
    ssd1306_clearScreen();
    ssd1306_displayOff();
    ESP_LOGI(TAG, "OLED display cleared and turned off");
#endif

    // Inicializar NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Inicializar SPIFFS
    ret = spiffs_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SPIFFS init returned %s", esp_err_to_name(ret));
        // non-fatal: continue but warn
    }

    // Criar event groups
    system_event_group = xEventGroupCreate();
    if (system_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create system_event_group");
        return ESP_FAIL;
    }

    // Criar queue para medições
    measurement_queue = xQueueCreate(20, sizeof(measurement_data_t)); 
    if (measurement_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create measurement queue");
        return ESP_FAIL;
    }

    // Criar mutex MQTT cedo para evitar tarefas tentarem usar antes de mqtt_init
    if (mqtt_mutex == NULL) {
        mqtt_mutex = xSemaphoreCreateMutex();
        if (mqtt_mutex == NULL) {
            ESP_LOGW(TAG, "Failed to create mqtt_mutex at init; tasks may log warnings until created");
        }
    }

    return ESP_OK;
}

// Helper para criar tasks com verificação de erro
static void create_task_checked(TaskFunction_t fn, const char *name, uint32_t stack, void *arg, UBaseType_t prio) {
    BaseType_t res = xTaskCreate(fn, name, stack, arg, prio, NULL);
    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task %s", name);
        // depending on policy, could restart or set an error flag
    } else {
        ESP_LOGI(TAG, "Task %s created (prio=%u)", name, (unsigned)prio);
    }
}

// Main application entry point 
void app_main(void) {

    if (init_system() != ESP_OK) {
        ESP_LOGE(TAG, "Critical initialization failed; rebooting...");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }

    // Inicializar sistema WiFi
    if (wifi_init_sta() != ESP_OK) {
        ESP_LOGE(TAG, "WiFi initialization failed, saving data to SPIFFS");
    }

    // Criar tasks
    create_task_checked(wifi_monitor_task, "wifi_monitor", TASK_STACK_SMALL, NULL, PRIO_WIFI);
    create_task_checked(ntp_sync_task, "ntp_sync", TASK_STACK_SMALL, NULL, PRIO_NTP);
#ifdef CONFIG_ENABLE_OLED_DISPLAY
    create_task_checked(oled_display_task, "oled_display", TASK_STACK_SMALL, NULL, PRIO_OLED);
#endif
    create_task_checked(measurement_task, "measurement", TASK_STACK_SMALL, NULL, PRIO_MEASUREMENT);
    create_task_checked(mqtt_monitor_task, "mqtt_monitor", TASK_STACK_SMALL, NULL, PRIO_MQTT_MON);
    create_task_checked(mqtt_publish_task, "mqtt_publish", TASK_STACK_MED, NULL, PRIO_MQTT_MON);
    create_task_checked(http_server_task, "http_server", TASK_STACK_SMALL, NULL, PRIO_HTTP);
    create_task_checked(system_status_task, "system_status", TASK_STACK_SMALL, NULL, PRIO_SYS_STATUS);
    create_task_checked(wifi_reconnect_manager_task, "wifi_reconnect_mgr", TASK_STACK_SMALL, NULL, PRIO_WIFI);

    ESP_LOGI(TAG, "All tasks created. System running...");
}
