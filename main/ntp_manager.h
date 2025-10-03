#ifndef NTP_MANAGER_H
#define NTP_MANAGER_H

#include <stdbool.h>
#include <sys/time.h>

/**
 * @brief Callback para eventos de sincronização de tempo
 * @param tv Pointer para timeval structure
 */
void time_sync_notification_cb(struct timeval *tv);

/**
 * @brief Inicializa o NTP
 */
void ntp_init(void);

/**
 * @brief Verifica se o tempo está sincronizado
 * @return true se sincronizado, false caso contrário
 */
bool is_time_synced(void);

/**
 * @brief Verifica se precisa forçar ressincronização
 * @return true se necessário ressincronizar
 */
bool ntp_needs_resync(void);

/**
 * @brief Força uma ressincronização imediata do NTP
 */
void ntp_force_sync(void);

/**
 * @brief Limpar cache de tempo antigo do NVS
 */
void ntp_clear_cache(void);

/**
 * @brief Aplica tempo em cache do NVS como fallback
 * @return true se aplicado com sucesso
 */
bool ntp_apply_cached_time(void);

/**
 * @brief Task de sincronização NTP
 * @param pvParameters Parâmetros da task (não utilizado)
 */
void ntp_sync_task(void *pvParameters);

#endif // NTP_MANAGER_H
