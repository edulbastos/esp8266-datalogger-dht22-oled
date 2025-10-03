#include "ntp_manager.h"
#include "globals.h"
#include "config.h"
#include "mqtt_manager.h"
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include <lwip/netdb.h>
#include <arpa/inet.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "time_cache.h"

void time_sync_notification_cb(struct timeval *tv) {
    static int sync_count = 0;
    sync_count++;
    
    time_t now = tv->tv_sec;
    struct tm timeinfo;
    char strftime_buf[64];
    
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    
    ESP_LOGI(TAG, "NTP sync #%d completed: %s (drift correction applied)", sync_count, strftime_buf);
    
    time_synced = true;
    current_state = NTP_SYNCED;
    xEventGroupSetBits(system_event_group, NTP_SYNCED_BIT);
    
    // Primeira sincronização: sinalizar para processar backlog armazenado (SPIFFS)
    if (sync_count == 1) {
        xEventGroupSetBits(system_event_group, PROCESS_BACKLOG_BIT);
        
        // NÃO regenerar client ID após sincronização para evitar LWT inesperado
        // O client ID já foi gerado na inicialização e deve permanecer o mesmo
        ESP_LOGI(TAG, "NTP synchronized, keeping existing client ID: %s", mqtt_client_id);
    }
    
    // Save synchronized time to NVS for next boot fallback
    if (now > 100000) {
        esp_err_t e = time_cache_save(now);
        if (e != ESP_OK) {
            ESP_LOGW(TAG, "Failed to save cached time: %s", esp_err_to_name(e));
        } else {
            ESP_LOGD(TAG, "Time cached to NVS for next boot");
        }
    }
}

void ntp_init(void) {
    ESP_LOGI(TAG, "Initializing NTP with Brazilian servers...");

    // Parar SNTP antes de reconfigurar para evitar core dump
    sntp_stop();

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    
    // Configurar intervalo de sincronização automática (1 hora)
    sntp_set_sync_interval(NTP_SYNC_INTERVAL_SEC * 1000); // ms

    // Try to resolve NTP hostnames first to avoid sntp's internal DNS overhead.
    for (int i = 0; i < 3; i++) {
        const char* server = (i == 0) ? NTP_SERVER1 : (i == 1) ? NTP_SERVER2 : NTP_SERVER3;
        struct hostent *he = gethostbyname(server);
        if (he != NULL && he->h_addr_list[0] != NULL) {
            struct in_addr addr;
            addr.s_addr = *((unsigned long *)he->h_addr_list[0]);
            const char *ipstr = inet_ntoa(addr);
            if (ipstr) {
                sntp_setservername(i, ipstr);
                ESP_LOGI(TAG, "NTP server %d resolved: %s -> %s", i+1, server, ipstr);
                continue;
            }
        }

        // Fallback to passing hostname; SNTP will resolve it internally
        sntp_setservername(i, server);
        ESP_LOGW(TAG, "Using NTP server hostname (not resolved quickly): %s", server);
    }

    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    sntp_init();
    
    ESP_LOGI(TAG, "NTP auto-sync configured for every %d seconds", NTP_SYNC_INTERVAL_SEC);

    // Set timezone to Brazil (UTC-3)
    setenv("TZ", "BRT3BRST,M10.3.0/0,M2.3.0/0", 1);
    tzset();

    current_state = NTP_SYNCING;
}

// Try loading cached time and apply as fallback (returns true if applied)
bool ntp_apply_cached_time(void) {
    time_t cached = 0;
    if (time_cache_load(&cached) == ESP_OK) {
        if (cached > 100000) {
            // Calcular idade do cache baseado no uptime aproximado
            // Se o dispositivo acabou de ligar, assumir que o cache pode ter até NTP_CACHE_MAX_AGE
            time_t uptime_sec = esp_timer_get_time() / 1000000; // uptime em segundos desde boot
            time_t estimated_current = cached + uptime_sec;
            
            // Verificar se o cache não é muito antigo (máximo 2 horas)
            if (uptime_sec < NTP_CACHE_MAX_AGE) {
                struct timeval tv = { .tv_sec = estimated_current, .tv_usec = 0 };
                settimeofday(&tv, NULL);
                
                struct tm timeinfo;
                char time_str[64];
                localtime_r(&estimated_current, &timeinfo);
                strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);
                
                ESP_LOGI(TAG, "Applied cached time (uptime +%us): %s", (uint32_t)uptime_sec, time_str);
                ESP_LOGW(TAG, "Using cached time - NTP sync required for accuracy");
                
                // Mark as synced enough for starting measurements but flag as needing real sync
                time_synced = false; // still not NTP-verified
                xEventGroupSetBits(system_event_group, NTP_SYNCED_BIT);
                return true;
            } else {
                ESP_LOGW(TAG, "Cached time too old (uptime: %us > %us), waiting for NTP sync", 
                         (uint32_t)uptime_sec, NTP_CACHE_MAX_AGE);
            }
        } else {
            ESP_LOGW(TAG, "Invalid cached time: %u", (uint32_t)cached);
        }
    } else {
        ESP_LOGI(TAG, "No cached time available, waiting for NTP sync");
    }
    return false;
}

bool is_time_synced(void) {
    time_t now = 0;
    time(&now);
    return (now > 1640995200); // Jan 1, 2022 00:00:00 UTC
}

// Verificar se precisa forçar uma ressincronização
bool ntp_needs_resync(void) {
    static time_t last_sync_check = 0;
    time_t now = time(NULL);
    
    // Primeira verificação
    if (last_sync_check == 0) {
        last_sync_check = now;
        return false;
    }
    
    // Verificar se passou muito tempo sem sincronização
    if ((now - last_sync_check) > NTP_RESYNC_THRESHOLD) {
        ESP_LOGW(TAG, "Time sync threshold exceeded (%d sec), forcing resync", 
                 (int)(now - last_sync_check));
        last_sync_check = now;
        return true;
    }
    
    return false;
}

// Forçar uma ressincronização imediata
void ntp_force_sync(void) {
    ESP_LOGI(TAG, "Forcing immediate NTP sync...");
    sntp_stop();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Temporariamente usar intervalo mais rápido
    sntp_set_sync_interval(NTP_SYNC_INTERVAL_SEC_FAST * 1000);
    sntp_init();
    
    // Após 30 segundos, voltar ao intervalo normal
    vTaskDelay(pdMS_TO_TICKS(30000));
    sntp_set_sync_interval(NTP_SYNC_INTERVAL_SEC * 1000);
}

// Limpar cache de tempo antigo do NVS
void ntp_clear_cache(void) {
    esp_err_t err = time_cache_save(0); // Salvar 0 para invalidar cache
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Time cache cleared from NVS");
    } else {
        ESP_LOGW(TAG, "Failed to clear time cache: %s", esp_err_to_name(err));
    }
}

void ntp_sync_task(void *pvParameters) {
    ESP_LOGI(TAG, "NTP sync task started");

    // Aguardar WiFi conectar
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, false, portMAX_DELAY);

    // Verificar se há cache válido, caso contrário limpar
    bool cache_applied = ntp_apply_cached_time();
    
    // Se o cache foi aplicado, verificar se não está muito desatualizado
    if (cache_applied) {
        time_t current_time = time(NULL);
        struct tm timeinfo;
        char time_str[64];
        
        localtime_r(&current_time, &timeinfo);
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);
        
        ESP_LOGI(TAG, "Starting with cached time: %s", time_str);
        ESP_LOGI(TAG, "Cached time applied; NTP will sync in background for accuracy");
        
        // Verificar se é uma data obviamente errada (antes de 2024)
        if (current_time < 1704067200) { // 01 Jan 2024 00:00:00 GMT
            ESP_LOGW(TAG, "Cached time appears to be too old, clearing cache");
            ntp_clear_cache();
            cache_applied = false;
        }
    }

    const TickType_t short_retry = pdMS_TO_TICKS(10000); // 10s
    const TickType_t long_retry = pdMS_TO_TICKS(60000);  // 60s
    const TickType_t monitor_interval = pdMS_TO_TICKS(NTP_SYNC_INTERVAL_SEC * 1000); // Intervalo de monitoramento
    TickType_t elapsed = 0;
    bool initial_sync_done = false;

    while (1) {
        if (!time_synced && current_state >= WIFI_CONNECTED) {
            ESP_LOGI(TAG, "Waiting for time synchronization...");

            // Try quickly for the first minute (10s intervals), then back off to 60s
            TickType_t wait_for = (elapsed < pdMS_TO_TICKS(60000)) ? short_retry : long_retry;

            // Aguardar pela sincronização por 'wait_for'
            if (xEventGroupWaitBits(system_event_group, NTP_SYNCED_BIT, false, false, wait_for) & NTP_SYNCED_BIT) {
                
                time_t now;
                char strftime_buf[64];
                struct tm timeinfo;

                time(&now);
                localtime_r(&now, &timeinfo);
                strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
                ESP_LOGI(TAG, "Time synchronized: %s", strftime_buf);

                // Mark global flag so we don't re-enter the wait/log loop repeatedly.
                // The SNTP callback also sets this, but the event bit may be set
                // (e.g. when applying cached time) without the flag being true.
                time_synced = true;
                initial_sync_done = true;

                // Sistema pronto para medições
                xEventGroupSetBits(system_event_group, SYSTEM_READY_BIT);
                current_state = SYSTEM_READY;
                atomic_store(&system_ready, true);

                // MQTT init is handled centrally by mqtt_monitor_task; do not call mqtt_init() here

            } else {
                ESP_LOGW(TAG, "Time sync timeout, retrying...");
                // Reinicializar NTP se timeout
                sntp_stop();
                vTaskDelay(pdMS_TO_TICKS(1000));
                ntp_init();
                elapsed += wait_for;
            }
        } else if (initial_sync_done && time_synced) {
            // Após sincronização inicial, entrar em modo de monitoramento
            ESP_LOGI(TAG, "NTP sync monitoring active (checking every %d sec)", NTP_SYNC_INTERVAL_SEC);
            
            while (time_synced) {
                // Aguardar intervalo de monitoramento
                vTaskDelay(monitor_interval);
                
                // Verificar se WiFi ainda está conectado
                EventBits_t wifi_bits = xEventGroupGetBits(wifi_event_group);
                if (!(wifi_bits & WIFI_CONNECTED_BIT)) {
                    ESP_LOGW(TAG, "WiFi disconnected, pausing NTP monitoring");
                    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, false, portMAX_DELAY);
                    ESP_LOGI(TAG, "WiFi reconnected, resuming NTP monitoring");
                }
                
                // Verificar se precisa forçar ressincronização
                if (ntp_needs_resync()) {
                    ntp_force_sync();
                }
                
                // Log de status periódico
                time_t now = time(NULL);
                if (now > 1640995200) {
                    struct tm timeinfo;
                    char strftime_buf[64];
                    localtime_r(&now, &timeinfo);
                    strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
                    ESP_LOGI(TAG, "NTP monitor: Current time %s (auto-sync active)", strftime_buf);
                }
            }
        }

        // Small delay to allow other tasks; actual waits above control pacing
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
