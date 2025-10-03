#ifndef DNS_MANAGER_H
#define DNS_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Configura os servidores DNS alternativos
 */
void configure_dns_servers(void);

/**
 * @brief Testa a resolução DNS
 * @return ESP_OK se sucesso, código de erro caso contrário
 */
esp_err_t test_dns_resolution(void);

/**
 * @brief Carrega o IP do broker salvo em NVS (se existir)
 * @return ESP_OK se encontrado e carregado em mqtt_broker_ip, ESP_ERR_NVS_NOT_FOUND se n\u00e3o existir, outro erro caso ocorra problema
 */
esp_err_t dns_load_cached_broker_ip(void);

/**
 * @brief Salva o IP do broker em NVS para uso em reboot
 */
esp_err_t dns_save_cached_broker_ip(const char* ip);

/**
 * @brief Remove o cache do broker em NVS
 */
esp_err_t dns_clear_cached_broker_ip(void);

/**
 * @brief Testa se um IP/host é alcançável via TCP na porta especificada (wrapper público)
 */
bool dns_is_ip_reachable(const char* ip, uint16_t port, uint32_t timeout_ms);

#endif // DNS_MANAGER_H
