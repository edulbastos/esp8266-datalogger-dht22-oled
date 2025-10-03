#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdint.h>
#include "esp_event.h"
#include "esp_err.h"

/**
 * @brief Manipulador de eventos WiFi
 * @param arg Argumentos do handler
 * @param event_base Base do evento
 * @param event_id ID do evento
 * @param event_data Dados do evento
 */
void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

/**
 * @brief Inicializa o WiFi em modo station
 * @return ESP_OK on success, or an esp_err_t error code on failure
 */
esp_err_t wifi_init_sta(void);

/**
 * @brief Task de monitoramento WiFi
 * @param pvParameters Parâmetros da task (não utilizado)
 */
void wifi_monitor_task(void *pvParameters);

/**
 * @brief Task de gerenciamento de reconexão WiFi
 * @param pvParameters Parâmetros da task (não utilizado)
 */
void wifi_reconnect_manager_task(void *pvParameters);

#endif // WIFI_MANAGER_H
