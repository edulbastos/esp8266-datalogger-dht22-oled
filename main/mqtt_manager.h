#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_event.h"
#include "mqtt_client.h"
#include "types.h"

/**
 * @brief Gera um client ID único baseado no MAC + timestamp
 */
void generate_unique_client_id(void);

/**
 * @brief Verifica se o throttle MQTT permite o envio de novas mensagens
 * @return true se permitido, false caso contrário
 */
bool mqtt_throttle_check(void);

/**
 * @brief Reseta o batch de throttling MQTT
 */
void mqtt_throttle_reset_batch(void);

/**
 * @brief Handler para eventos MQTT
 * @param handler_args Argumentos do handler
 * @param base Base do evento
 * @param event_id ID do evento
 * @param event_data Dados do evento
 */
void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

/**
 * @brief Inicializa o cliente MQTT
 */
void mqtt_init(void);

/**
 * @brief Publica uma medição MQTT
 * @param measurement Ponteiro para a medição a ser publicada
 * @return true se sucesso, false caso contrário
 */
bool mqtt_publish_measurement(const measurement_data_t* measurement);

/**
 * @brief Task de publicação MQTT com throttling
 * @param pvParameters Parâmetros da task (não utilizado)
 */
void mqtt_publish_task(void *pvParameters);

/**
 * @brief Task de monitoramento MQTT
 * @param pvParameters Parâmetros da task (não utilizado)
 */
void mqtt_monitor_task(void *pvParameters);

#endif // MQTT_MANAGER_H
