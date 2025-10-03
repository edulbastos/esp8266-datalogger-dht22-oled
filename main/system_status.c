#include "system_status.h"
#include "globals.h"
#include "config.h"
#include "spiffs_manager.h"
#include <time.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

void system_status_task(void *pvParameters) {
    ESP_LOGI(TAG, "System status task started (interval: %d ms)", SYSTEM_STATUS_INTERVAL_MS);

#if !SYSTEM_STATUS_ENABLED
    ESP_LOGW(TAG, "System status task disabled, suspending task");
    vTaskSuspend(NULL);
    return;
#endif

    while (1) {
        ESP_LOGI(TAG, "=== System Status ===");
        
        // Yield para permitir outras tasks executarem
        vTaskDelay(pdMS_TO_TICKS(10));
        
        ESP_LOGI(TAG, "State: %d", current_state);
        ESP_LOGI(TAG, "Time synced: %s", time_synced ? "YES" : "NO");
        ESP_LOGI(TAG, "Measurements generated: %u", measurement_counter);
        ESP_LOGI(TAG, "Client ID: %s", mqtt_client_id);
        
        // Yield antes de operações de Event Group
        vTaskDelay(pdMS_TO_TICKS(10));
        
        EventBits_t wifi_bits = xEventGroupGetBits(wifi_event_group);
        ESP_LOGI(TAG, "WiFi Connected: %s", (wifi_bits & WIFI_CONNECTED_BIT) ? "YES" : "NO");
        
        EventBits_t bits = xEventGroupGetBits(system_event_group);
        ESP_LOGI(TAG, "MQTT Connected: %s", (bits & MQTT_CONNECTED_BIT) ? "YES" : "NO");
        ESP_LOGI(TAG, "MQTT Messages sent: %u", mqtt_messages_sent);
        ESP_LOGI(TAG, "MQTT Batch: %d/%d", mqtt_batch_count, MQTT_BATCH_SIZE);
        
        // Yield antes de operações SPIFFS
        vTaskDelay(pdMS_TO_TICKS(10));
        
        if (spiffs_initialized) {
            spiffs_print_status();
        }

        // Yield antes de operações de tempo
        vTaskDelay(pdMS_TO_TICKS(10));

        // Mostrar hora atual se sincronizada
        if (time_synced) {
            time_t now;
            char strftime_buf[64];
            struct tm timeinfo;

            time(&now);
            localtime_r(&now, &timeinfo);
            strftime(strftime_buf, sizeof(strftime_buf), "%d/%m/%Y %H:%M:%S", &timeinfo);
            ESP_LOGI(TAG, "Current time: %s", strftime_buf);
        }

        // Verificar uso de memória
        ESP_LOGI(TAG, "Free heap: %d bytes", esp_get_free_heap_size());
        ESP_LOGI(TAG, "==================");

        // Usar configuração do config.h
        vTaskDelay(pdMS_TO_TICKS(SYSTEM_STATUS_INTERVAL_MS));
    }
}
