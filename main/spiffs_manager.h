#ifndef SPIFFS_MANAGER_H
#define SPIFFS_MANAGER_H

#include "esp_err.h"
#include "types.h"

/**
 * @brief Inicializa o sistema SPIFFS
 * @return ESP_OK se sucesso, código de erro caso contrário
 */
esp_err_t spiffs_init(void);

/**
 * @brief Carrega o índice do ringbuffer SPIFFS
 * @return ESP_OK se sucesso, código de erro caso contrário
 */
esp_err_t load_spiffs_index(void);

/**
 * @brief Salva o índice do ringbuffer SPIFFS
 * @return ESP_OK se sucesso, código de erro caso contrário
 */
esp_err_t save_spiffs_index(void);

/**
 * @brief Armazena uma medição no SPIFFS
 * @param measurement Ponteiro para a medição a ser armazenada
 * @return ESP_OK se sucesso, código de erro caso contrário
 */
esp_err_t spiffs_store_measurement(const measurement_data_t* measurement);

/**
 * @brief Obtém a próxima medição do SPIFFS
 * @param measurement Ponteiro para onde armazenar a medição lida
 * @return ESP_OK se sucesso, ESP_ERR_NOT_FOUND se não há medições
 */
esp_err_t spiffs_get_next_measurement(measurement_data_t* measurement);

/**
 * @brief Obtém e remove imediatamente a próxima medição do SPIFFS (para envio MQTT)
 * @param measurement Ponteiro para onde armazenar a medição lida
 * @return ESP_OK se sucesso, ESP_ERR_NOT_FOUND se não há medições
 */
esp_err_t spiffs_get_and_remove_next_measurement(measurement_data_t* measurement);

/**
 * @brief Re-adiciona uma medição no início da fila (rollback em caso de falha de envio)
 * @param measurement Ponteiro para a medição a ser re-adicionada
 * @return ESP_OK se sucesso, código de erro caso contrário
 */
esp_err_t spiffs_rollback_measurement(const measurement_data_t* measurement);

/**
 * @brief Remove uma medição enviada do SPIFFS
 * @return ESP_OK se sucesso, código de erro caso contrário
 */
esp_err_t spiffs_remove_sent_measurement(void);

/**
 * @brief Imprime o status do SPIFFS
 */
void spiffs_print_status(void);

#endif // SPIFFS_MANAGER_H
