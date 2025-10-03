#include "measurement.h"
#include "globals.h"
#include "config.h"
#include <time.h>
#include <string.h>
#include "esp_log.h"
#include "spiffs_manager.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "dht.h"  // Nova biblioteca DHT

void measurement_task(void *pvParameters) {
    measurement_data_t measurement;
    uint8_t mac[6];
    
    // Obter MAC address
    esp_wifi_get_mac(WIFI_IF_STA, mac);

    ESP_LOGI(TAG, "Measurement task started. Waiting for time sync (timeout %d s)...", 15);

    // Espera a sincronização do NTP por até 15s; se não ocorrer, continua com uptime timestamps
    EventBits_t bits = xEventGroupWaitBits(system_event_group, NTP_SYNCED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));
    if (bits & NTP_SYNCED_BIT) {
        ESP_LOGI(TAG, "Time synced. Starting measurements with Client ID: %s", mqtt_client_id);
    } else {
        ESP_LOGW(TAG, "Time sync timeout after 15s; starting measurements with uptime-based timestamps");
    }

    // Measurements will start after NTP sync; mqtt_publish_task
    // will handle sending or storing in SPIFFS when MQTT is unavailable.

    while (1) {
        // Sempre gerar medições: usar tempo real quando sincronizado, caso contrário usar uptime em segundos
        uint32_t ts;
        time_t current_time = time(NULL);
        
        // Validação de sanidade do timestamp
        bool time_is_sane = (current_time > 1704067200 && current_time < 1893456000); // 2024-2030
        
        if (time_synced && time_is_sane) {
#if USE_LOCAL_TIMESTAMP
            // Aplicar timezone GMT-3 (subtrair 3 horas do UTC para enviar horário local)
            struct tm local_tm;
            localtime_r(&current_time, &local_tm);
            ts = (uint32_t)mktime(&local_tm);
            ESP_LOGD(TAG, "Using Local time (GMT-3): %u", ts);
#else
            // Usar UTC (padrão recomendado para IoT)
            ts = (uint32_t)current_time;
            ESP_LOGD(TAG, "Using UTC time: %u", ts);
#endif
            
            // Log para mostrar diferença UTC vs Local
            struct tm utc_tm, local_tm;
            char utc_str[32], local_str[32];
            gmtime_r(&current_time, &utc_tm);
            localtime_r(&current_time, &local_tm);
            strftime(utc_str, sizeof(utc_str), "%H:%M:%S", &utc_tm);
            strftime(local_str, sizeof(local_str), "%H:%M:%S", &local_tm);
            
            ESP_LOGI(TAG, "Timestamp info - UTC: %s, Local: %s, Sent: %u (%s)", 
                     utc_str, local_str, ts, USE_LOCAL_TIMESTAMP ? "Local" : "UTC");
        } else {
            /*
             * xTaskGetTickCount() returns ticks; to convert to seconds:
             * uptime_ms = ticks * portTICK_PERIOD_MS
             * seconds = uptime_ms / 1000
             */
            uint32_t uptime_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
            ts = uptime_ms / 1000U;
            
            if (time_synced && !time_is_sane) {
                ESP_LOGW(TAG, "Time appears to be invalid (%u), using uptime: %u", (uint32_t)current_time, ts);
            } else {
                ESP_LOGW(TAG, "Time not synced, using uptime-based timestamp (s since boot): %u", ts);
            }
        }

        // Ler DHT22 físico; se falhar, fazer fallback para simulação
        float temperature = 0.0f;
        float humidity = 0.0f;
        esp_err_t dht_err = dht_read_float_data(DHT_TYPE_AM2301, DHT22_PIN, &humidity, &temperature);
        if (dht_err != ESP_OK) {
            // Fallback para simulação (mantém comportamento anterior)
            ESP_LOGW(TAG, "DHT22 read failed (%s). Falling back to simulated values.", esp_err_to_name(dht_err));
            temperature = 20.0f + (esp_random() % 100) / 10.0f; // 20-30°C
            humidity = 40.0f + (esp_random() % 400) / 10.0f;    // 40-80%
        } else {
            ESP_LOGI(TAG, "DHT22 read successful: T=%.1f°C, H=%.1f%%", temperature, humidity);
        }

        // Preparar medição
        measurement.timestamp = ts;
        strcpy(measurement.sensor_id, SENSOR_ID);
        memcpy(measurement.mac_address, mac, 6);
        measurement.temperature = temperature;
        measurement.humidity = humidity;
        measurement.retry_count = 0;
        measurement.measurement_id = ++measurement_counter;

        ESP_LOGI(TAG, "New measurement: %.1f°C, %.1f%% (ID: %u, timestamp: %u)", 
                 temperature, humidity, measurement.measurement_id, ts);
                 
        // Validar se timestamp é razoável 
        time_t current_real_time = time(NULL);
        if (current_real_time > 1704067200 && ts > 1893456000) { // 2030
            ESP_LOGW(TAG, "WARNING: Timestamp appears to be in the future: %u (real time: %u)", 
                     ts, (uint32_t)current_real_time);
        }

        // Enviar para queue (o mqtt_publish_task decide se publica ou grava em SPIFFS)
        // Verificar se a queue está muito cheia antes de enviar
    UBaseType_t queue_remaining = uxQueueSpacesAvailable(measurement_queue);
        
        if (queue_remaining < 2) {
            ESP_LOGW(TAG, "Queue almost full (%d remaining)! MQTT publish may be slow", queue_remaining);
        }
        
        if (xQueueSend(measurement_queue, &measurement, pdMS_TO_TICKS(1000)) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to send measurement to queue - queue may be full!");
        } else {
            ESP_LOGI(TAG, "Measurement queued successfully (queue: %d/20 used)", 
                     uxQueueMessagesWaiting(measurement_queue));
        }

        // Atualizar variáveis globais
        g_last_temperature = temperature;
        g_last_humidity = humidity;

        // Atualizar última medição global
        last_measurement = measurement;
        
        // Monitorar uso de memória após cada medição
        uint32_t free_heap = esp_get_free_heap_size();
        static uint32_t min_heap = UINT32_MAX;
        if (free_heap < min_heap) {
            min_heap = free_heap;
            /* Demote to debug to avoid spamming logs with warnings about heap minima */
            ESP_LOGD(TAG, "New minimum heap: %u bytes", min_heap);
        }
        if (measurement_counter % 10 == 0) { // Log a cada 10 medições
            ESP_LOGD(TAG, "Memory status: current=%u, minimum=%u bytes", free_heap, min_heap);
        }
        
        vTaskDelay(pdMS_TO_TICKS(MEASUREMENT_INTERVAL_MS));
    }
}
