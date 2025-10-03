#include "mqtt_manager.h"
#include "globals.h"
#include "config.h"
#include "spiffs_manager.h"
#include "dns_manager.h"
#include <time.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "mqtt_client.h"

// Global para rastrear última atividade MQTT (para heartbeat)
static TickType_t last_mqtt_activity_time = 0;

// MQTT client handle (global)
void mqtt_throttle_reset_batch() {
    mqtt_batch_count = 0;
    last_batch_time = xTaskGetTickCount();
}

// Helper: safely stop/destroy mqtt_client while respecting mqtt_mutex when available
// Prefer stopping the client only (no destroy) to avoid crashes observed in
// esp_mqtt_client_destroy on ESP8266 builds. We still coordinate with mqtt_mutex
// so publishers won't race with the stop.
static void safe_stop_mqtt_client(void) {
    if (mqtt_client == NULL) return;
    if (mqtt_mutex) {
        const int attempts = 5;
        for (int i = 0; i < attempts; ++i) {
            if (xSemaphoreTake(mqtt_mutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
                if (system_event_group) {
                    xEventGroupClearBits(system_event_group, MQTT_CONNECTED_BIT);
                }
                current_state = MQTT_CONNECTING;
                esp_mqtt_client_stop(mqtt_client);
                // Do not call esp_mqtt_client_destroy() on ESP8266 — keep handle until
                // mqtt_init recreates or reinitializes it to avoid LoadProhibited crashes.
                xSemaphoreGive(mqtt_mutex);
                return;
            }
            ESP_LOGW(TAG, "safe_stop_mqtt_client: attempt %d/%d failed to take mqtt_mutex", i+1, attempts);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        ESP_LOGW(TAG, "safe_stop_mqtt_client: could not take mqtt_mutex after %d attempts; skipping stop to avoid race", attempts);
        return;
    }
    ESP_LOGW(TAG, "safe_stop_mqtt_client: mqtt_mutex not created; skipping stop to avoid race");
}

// Gerar um client ID único baseado no MAC e timestamp
void generate_unique_client_id(void) {
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    
    // Usar timestamp atual + MAC + random para garantir unicidade máxima
    uint32_t timestamp = (uint32_t)time(NULL);
    if (timestamp < 1640995200) { // Se tempo não sincronizado, usar uptime
        timestamp = xTaskGetTickCount() / portTICK_PERIOD_MS;
    }
    
    // Adicionar número aleatório para evitar conflitos
    uint32_t random_suffix = esp_random() & 0xFFFF;
    
    // Formato: esp8266_dl_AABBCC_12345678_RRRR
    snprintf(mqtt_client_id, sizeof(mqtt_client_id), 
             "%s_%02X%02X%02X_%08X_%04X", 
             MQTT_CLIENT_ID_PREFIX,
             mac[3], mac[4], mac[5],  // Últimos 3 bytes do MAC
             timestamp,
             random_suffix);
    
    ESP_LOGI(TAG, "Generated unique client ID: %s", mqtt_client_id);
}

// Verificar se podemos enviar mais mensagens no batch atual
bool mqtt_throttle_check(void) {
    TickType_t current_time = xTaskGetTickCount();
    
    // Se é o primeiro batch ou batch foi resetado, permitir
    if (mqtt_batch_count == 0) {
        return true;  // Não precisa resetar novamente, já está zerado
    }
    
    // Se batch está cheio, verificar se já passou o delay
    if (mqtt_batch_count >= MQTT_BATCH_SIZE) {
        if ((current_time - last_batch_time) >= pdMS_TO_TICKS(MQTT_BATCH_DELAY_MS)) {
            mqtt_throttle_reset_batch();
            return true;
        } else {
            return false;
        }
    }
    
    // Se ainda há slots no batch atual
    if (mqtt_batch_count < MQTT_BATCH_SIZE) {
        return true;
    }
    
    // Fallback - não deveria chegar aqui
    return false;
}

// Callback para eventos MQTT
void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    // Track consecutive connection failures to decide when to clear DNS cache
    static int mqtt_consec_failures = 0;
    const int MQTT_CLEAR_CACHE_THRESHOLD = 3;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT Connected successfully with client ID: %s", mqtt_client_id);
        current_state = MQTT_CONNECTED;
        xEventGroupSetBits(system_event_group, MQTT_CONNECTED_BIT);

        // Solicitar processamento imediato do backlog armazenado
        xEventGroupSetBits(system_event_group, PROCESS_BACKLOG_BIT);
        // reset consecutive failure counter
        mqtt_consec_failures = 0;
        
        // Reset throttling
        mqtt_throttle_reset_batch();
        
        // Publicar status de conexão
        esp_mqtt_client_publish(client, MQTT_TOPIC_STATUS, "Online", 0, 1, 1);
        ESP_LOGI(TAG, "MQTT status published: Online");
        
        // Atualizar timestamp da última atividade MQTT  
        last_mqtt_activity_time = xTaskGetTickCount();
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT Disconnected");
        xEventGroupClearBits(system_event_group, MQTT_CONNECTED_BIT);
        current_state = MQTT_CONNECTING;
        
        // Log possíveis causas da desconexão (informational)
        ESP_LOGI(TAG, "Disconnect could be due to: keep-alive timeout, network issue, or broker restart");
        ESP_LOGI(TAG, "Keep-alive configured: 60s, check if broker received data within this window");
        
        // increment failure counter and clear cached broker IP if threshold reached
        mqtt_consec_failures++;
        ESP_LOGI(TAG, "MQTT consecutive failures: %d", mqtt_consec_failures);
        if (mqtt_consec_failures >= MQTT_CLEAR_CACHE_THRESHOLD) {
            ESP_LOGW(TAG, "MQTT failed %d times; clearing cached broker IP to force DNS re-resolution", mqtt_consec_failures);
            esp_err_t e = dns_clear_cached_broker_ip();
            if (e == ESP_OK) {
                ESP_LOGI(TAG, "Cleared broker IP cache in NVS");
            } else {
                ESP_LOGW(TAG, "Failed to clear broker IP cache: %s", esp_err_to_name(e));
            }
            mqtt_consec_failures = 0; // reset after clearing
        }
        break;

    case MQTT_EVENT_PUBLISHED: {
        ESP_LOGD(TAG, "MQTT message published, msg_id=%d", event->msg_id);

        // Apenas contar mensagens que correspondem a uma medição pendente (assim não contamos status/LWT)
        bool matched = false;
        for (int i = 0; i < mqtt_pending_count; i++) {
            if (mqtt_pending_msgs[i].msg_id == event->msg_id) {
                matched = true;
                // Incrementar apenas o total de mensagens confirmadas
                mqtt_messages_sent++;
                // Note: mqtt_batch_count já foi incrementado no envio

                ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED: msg_id=%d confirmed, measurement_id=%u -> mqtt_messages_sent=%d",
                         event->msg_id, mqtt_pending_msgs[i].measurement.measurement_id, mqtt_messages_sent);

                if (mqtt_pending_msgs[i].is_stored) {
                    // Nota: A medição já foi removida do SPIFFS antes do envio (nova lógica)
                    ESP_LOGI(TAG, "Stored measurement confirmed (ID: %u) - already removed from SPIFFS", 
                             mqtt_pending_msgs[i].measurement.measurement_id);
                }

                // Remover da fila pendente APENAS após confirmação
                for (int j = i; j < mqtt_pending_count - 1; j++) {
                    mqtt_pending_msgs[j] = mqtt_pending_msgs[j+1];
                }
                mqtt_pending_count--;
                break;
            }
        }

        if (!matched) {
            ESP_LOGD(TAG, "Published msg_id %d not found in pending list (likely status/LWT)", event->msg_id);
        }
        break;
    }

    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT Error occurred");
        
        // Log detalhes do erro de forma compatível com ESP8266
        if (event->error_handle) {
            ESP_LOGE(TAG, "MQTT error type: %d", event->error_handle->error_type);
            ESP_LOGD(TAG, "MQTT error handle: esp_tls_last_esp_err=%d, esp_tls_stack_err=%d",
                     event->error_handle->esp_tls_last_esp_err,
                     event->error_handle->esp_tls_stack_err);
            
            // Verificar diferentes tipos de erro compatíveis com ESP8266
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                ESP_LOGE(TAG, "MQTT connection refused by broker");
            } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_ESP_TLS) {
                ESP_LOGE(TAG, "MQTT TLS error");
            } else {
                ESP_LOGE(TAG, "MQTT transport/network error");
            }
        } else {
            ESP_LOGE(TAG, "MQTT error - no error handle available");
        }
        
        // count this as a failure event too
        mqtt_consec_failures++;
        ESP_LOGI(TAG, "MQTT consecutive failures (error): %d", mqtt_consec_failures);
        if (mqtt_consec_failures >= MQTT_CLEAR_CACHE_THRESHOLD) {
            ESP_LOGW(TAG, "MQTT error threshold reached; clearing cached broker IP to force DNS re-resolution");
            esp_err_t e = dns_clear_cached_broker_ip();
            if (e == ESP_OK) ESP_LOGI(TAG, "Cleared broker IP cache in NVS");
            else ESP_LOGW(TAG, "Failed to clear broker IP cache: %s", esp_err_to_name(e));
            mqtt_consec_failures = 0;
        }
        ESP_LOGD(TAG, "MQTT error will be handled by monitor task");
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGD(TAG, "MQTT data received: topic=%.*s, data=%.*s", 
                 event->topic_len, event->topic, event->data_len, event->data);
        break;

    default:
        ESP_LOGD(TAG, "MQTT event: %d", event_id);
        break;
    }
}

// Inicializar o cliente MQTT
void mqtt_init(void) {

    if (strlen(mqtt_client_id) == 0) {
        generate_unique_client_id();
    }

    // If we already have a client and it's connected, do nothing
    if (mqtt_client) {
        EventBits_t bits = xEventGroupGetBits(system_event_group);
        if ((bits & MQTT_CONNECTED_BIT) != 0) {
            ESP_LOGI(TAG, "mqtt_init: client already connected, skipping init");
            return;
        }
    }

    ESP_LOGI(TAG, "Initializing MQTT client with ID: %s", mqtt_client_id);

    // Usar IP se DNS falhar, senão usar hostname
    char broker_uri[256];
    if (strlen(mqtt_broker_ip) > 0) {
        snprintf(broker_uri, sizeof(broker_uri), "mqtt://%s:1883", mqtt_broker_ip);
        ESP_LOGI(TAG, "Using resolved IP for MQTT: %s", broker_uri);
    } else {
        strcpy(broker_uri, MQTT_BROKER);
        ESP_LOGI(TAG, "Using hostname for MQTT: %s", broker_uri);
    }

    ESP_LOGD(TAG, "MQTT config: uri=%s, client_id=%s, username=%s", broker_uri, mqtt_client_id, MQTT_USERNAME?MQTT_USERNAME:"(nil)");

    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = broker_uri,
        .client_id = mqtt_client_id,
        .keepalive = MQTT_KEEPALIVE_SEC,
        .username = MQTT_USERNAME,
        .password = MQTT_PASSWORD,
        .lwt_topic = MQTT_TOPIC_STATUS,
        .lwt_msg = "Offline",
        .lwt_qos = 1,
        .lwt_retain = 1,
        .task_stack = 6144,
        .buffer_size = 1024,
    };

    // If a client already exists, prefer stopping cleanly then reconnecting
    if (mqtt_client) {
        ESP_LOGI(TAG, "mqtt_client already exists, stopping cleanly before reconnect");
        // Parar cliente existente de forma limpa para evitar LWT inesperado
        if (xSemaphoreTake(mqtt_mutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
            esp_mqtt_client_stop(mqtt_client);
            esp_mqtt_client_destroy(mqtt_client);
            mqtt_client = NULL;
            xSemaphoreGive(mqtt_mutex);
            ESP_LOGI(TAG, "Previous MQTT client stopped and destroyed cleanly");
        } else {
            ESP_LOGW(TAG, "Could not take mutex to stop previous client cleanly");
        }
        // Aguardar um pouco para garantir desconexão limpa
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client (esp_mqtt_client_init returned NULL)");
        return;
    }

    ESP_LOGD(TAG, "esp_mqtt_client initialized: %p", mqtt_client);

    // Criar mutex para MQTT se não existir
    if (mqtt_mutex == NULL) {
        mqtt_mutex = xSemaphoreCreateMutex();
    }

    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    
    esp_err_t err = esp_mqtt_client_start(mqtt_client);
    if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start MQTT client: %s (err=0x%X)", esp_err_to_name(err), err);
    // Stop the client safely; avoid destroy on ESP8266 which has caused crashes
    safe_stop_mqtt_client();
    mqtt_client = NULL;
        return;
    }
    
    current_state = MQTT_CONNECTING;
    ESP_LOGI(TAG, "MQTT client started successfully");
}

//  Publicar uma medição via MQTT
bool mqtt_publish_measurement(const measurement_data_t* measurement) {
    if (!mqtt_client || !measurement) {
        return false;
    }

    // Mensagens novas não devem ser throttled - sempre tem prioridade
    // Throttling é aplicado apenas para mensagens do SPIFFS (batch)

    if (xSemaphoreTake(mqtt_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to take MQTT mutex");
        return false;
    }

    bool success = false;

    // Criar JSON da medição
    char json_data[512];
    snprintf(json_data, sizeof(json_data),
        "{"
        "\"client_id\":\"%s\","
        "\"sensor_id\":\"%s\","
        "\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\","
        "\"timestamp\":%u,"
        "\"temperature\":%.2f,"
        "\"humidity\":%.2f,"
        "\"measurement_id\":%u"
        "}",
        mqtt_client_id,
        measurement->sensor_id,
        measurement->mac_address[0], measurement->mac_address[1],
        measurement->mac_address[2], measurement->mac_address[3],
        measurement->mac_address[4], measurement->mac_address[5],
        measurement->timestamp,
        measurement->temperature,
        measurement->humidity,
        measurement->measurement_id
    );

    // Usar QoS 1 para garantir confirmação do broker
    int msg_id = -1;

    if (mqtt_client == NULL) {
        ESP_LOGI(TAG, "mqtt_client is NULL, cannot publish");
        xSemaphoreGive(mqtt_mutex);
        return false;
    }

    msg_id = esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_DATA, json_data, 0, 1, 0);
    ESP_LOGD(TAG, "esp_mqtt_client_publish returned msg_id=%d, mqtt_publish_attempts=%u", msg_id, mqtt_publish_attempts);

    if (msg_id >= 0) {
        mqtt_publish_attempts++;
        // NÃO incrementar batch_count para mensagens novas individuais
        // batch_count é usado apenas para throttling de SPIFFS
        
        // Atualizar timestamp da última atividade MQTT
        last_mqtt_activity_time = xTaskGetTickCount();
        
        ESP_LOGI(TAG, "Published: %.1f°C, %.1f%%, ID=%u (msg_id=%d, pending=%d)", 
                 measurement->temperature, measurement->humidity, 
                 measurement->measurement_id, msg_id, mqtt_pending_count);
                 
        success = true;
        
        // Adicionar à fila de pendentes
        if (mqtt_pending_count < MAX_PENDING_MSGS) {
            mqtt_pending_msgs[mqtt_pending_count].msg_id = msg_id;
            mqtt_pending_msgs[mqtt_pending_count].measurement = *measurement;
            mqtt_pending_msgs[mqtt_pending_count].is_stored = false;
            mqtt_pending_count++;
        }
        
        // Delay entre mensagens no batch
        if (mqtt_batch_count < MQTT_BATCH_SIZE) {
            vTaskDelay(pdMS_TO_TICKS(MQTT_MESSAGE_DELAY_MS));
        }
        

    } else {
        ESP_LOGE(TAG, "Failed to publish measurement, msg_id=%d", msg_id);
    }

    xSemaphoreGive(mqtt_mutex);
    return success;
}

//  Tarefa principal de publicação MQTT
void mqtt_publish_task(void *pvParameters) {
    measurement_data_t measurement;
    bool mqtt_connected = false;
    bool processing_spiffs = false;
    uint32_t failed_publishes = 0;
    uint32_t stored_sent = 0;
    
    ESP_LOGI(TAG, "MQTT publish task started (simplified version)");
    last_mqtt_activity_time = xTaskGetTickCount();

        while (1) {
        // Atualizar estado da conexão MQTT
        EventBits_t bits = xEventGroupGetBits(system_event_group);
        mqtt_connected = (bits & MQTT_CONNECTED_BIT) != 0;
        bool process_backlog = (bits & PROCESS_BACKLOG_BIT) != 0;
        
        // === HEARTBEAT SIMPLIFICADO ===
        TickType_t now = xTaskGetTickCount();
        uint32_t seconds_since_activity = (now - last_mqtt_activity_time) * portTICK_PERIOD_MS / 1000;
        
        if (mqtt_connected && seconds_since_activity > MQTT_HEARTBEAT_INTERVAL) {
            // Heartbeat simples apenas para manter conexão
            if (xSemaphoreTake(mqtt_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                if (mqtt_client) {
                    int heartbeat_id = esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_STATUS, "OK", 0, 0, 0);
                    if (heartbeat_id >= 0) {
                        last_mqtt_activity_time = now;
                        ESP_LOGD(TAG, "Heartbeat sent");
                    }
                }
                xSemaphoreGive(mqtt_mutex);
            }
        }

        // === PRIORIDADE 1: NOVAS MEDIÇÕES (sempre interrompem SPIFFS) ===
        if (xQueuePeek(measurement_queue, &measurement, pdMS_TO_TICKS(10)) == pdTRUE) {
            xQueueReceive(measurement_queue, &measurement, 0); // Remove da queue
            

            // Interromper processamento SPIFFS se estiver ativo
            if (processing_spiffs) {
                processing_spiffs = false;
                ESP_LOGI(TAG, "SPIFFS processing interrupted by new measurement ID %u", 
                         measurement.measurement_id);
            }
            
            if (mqtt_connected) {
                // MQTT disponível - tentar envio direto
                if (mqtt_publish_measurement(&measurement)) {
                    ESP_LOGI(TAG, "New measurement sent directly (ID: %u, batch_count: %d)", 
                             measurement.measurement_id, mqtt_batch_count);
                } else {
                    // Falha no envio - armazenar em SPIFFS
                    ESP_LOGW(TAG, "Failed to send new measurement ID %u, storing in SPIFFS", 
                             measurement.measurement_id);
                    spiffs_store_measurement(&measurement);
                    failed_publishes++;
                }
            } else {
                // MQTT indisponível - armazenar em SPIFFS
                ESP_LOGD(TAG, "MQTT not available, storing measurement ID %u in SPIFFS", 
                         measurement.measurement_id);
                spiffs_store_measurement(&measurement);
            }
            continue; // Verificar imediatamente se há mais medições novas
        }

        // === PRIORIDADE 2: PROCESSAR SPIFFS (apenas quando MQTT disponível e sem novas medições) ===
        if (mqtt_connected && ring_idx.count > 0 && !processing_spiffs) {
            processing_spiffs = true;
            ESP_LOGI(TAG, "Starting SPIFFS processing (%d messages pending)", ring_idx.count);
        }

        if (processing_spiffs && mqtt_connected && ring_idx.count > 0) {
            // Verificar throttling apenas para SPIFFS
            if (mqtt_throttle_check()) {
                measurement_data_t stored_measurement;
                
                // Obter e remover medição do SPIFFS
                if (spiffs_get_and_remove_next_measurement(&stored_measurement) == ESP_OK) {
                    ESP_LOGI(TAG, "Sending stored measurement (ID: %u)", stored_measurement.measurement_id);
                    
                    // Criar JSON para medição armazenada
                    char json_data[512];
                    
                    snprintf(json_data, sizeof(json_data),
                        "{"
                        "\"client_id\":\"%s\","
                        "\"sensor_id\":\"%s\","
                        "\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\","
                        "\"timestamp\":%u,"
                        "\"temperature\":%.2f,"
                        "\"humidity\":%.2f,"
                        "\"measurement_id\":%u"
                        "}",
                        mqtt_client_id,
                        stored_measurement.sensor_id,
                        stored_measurement.mac_address[0], stored_measurement.mac_address[1],
                        stored_measurement.mac_address[2], stored_measurement.mac_address[3],
                        stored_measurement.mac_address[4], stored_measurement.mac_address[5],
                        stored_measurement.timestamp,
                        stored_measurement.temperature,
                        stored_measurement.humidity,
                        stored_measurement.measurement_id
                    );
                    
                    // Enviar via MQTT
                    int msg_id = -1;
                    if (xSemaphoreTake(mqtt_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                        if (mqtt_client != NULL) {
                            msg_id = esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_DATA, json_data, 0, 1, 0);
                        }
                        xSemaphoreGive(mqtt_mutex);
                    }
                    
                    if (msg_id >= 0) {
                        // Sucesso - atualizar contadores
                        mqtt_publish_attempts++;
                        mqtt_batch_count++;
                        last_mqtt_activity_time = xTaskGetTickCount();
                        
                        // Aplicar throttling batch apenas no SPIFFS
                        if (mqtt_batch_count >= MQTT_BATCH_SIZE) {
                            last_batch_time = xTaskGetTickCount();
                            mqtt_throttle_reset_batch();
                        }
                        
                        // Adicionar à fila de pendentes para confirmação
                        if (mqtt_pending_count < MAX_PENDING_MSGS) {
                            mqtt_pending_msgs[mqtt_pending_count].msg_id = msg_id;
                            mqtt_pending_msgs[mqtt_pending_count].measurement = stored_measurement;
                            mqtt_pending_msgs[mqtt_pending_count].is_stored = true;
                            mqtt_pending_count++;
                            stored_sent++;
                        }
                        
                        ESP_LOGI(TAG, "Stored measurement sent, awaiting confirmation (ID: %u)", 
                                stored_measurement.measurement_id);
                    } else {
                        // Falha no envio - fazer rollback para SPIFFS
                        ESP_LOGW(TAG, "Failed to send stored measurement, rolling back to SPIFFS");
                        if (spiffs_rollback_measurement(&stored_measurement) != ESP_OK) {
                            ESP_LOGE(TAG, "Failed to rollback measurement ID %u", stored_measurement.measurement_id);
                        }
                    }
                } else {
                    // Não há mais medições no SPIFFS
                    processing_spiffs = false;
                    ESP_LOGI(TAG, "SPIFFS processing completed - no more stored messages");
                }
            }
        }

        // Parar processamento SPIFFS se MQTT desconectou
        if (!mqtt_connected && processing_spiffs) {
            processing_spiffs = false;
            ESP_LOGW(TAG, "SPIFFS processing stopped - MQTT disconnected");
        }

        // Limpar bit de processamento de backlog se foi processado
        if (process_backlog) {
            xEventGroupClearBits(system_event_group, PROCESS_BACKLOG_BIT);
        }

        // === CONTROLE DE THROTTLING PARA SPIFFS ===
        if (processing_spiffs && mqtt_batch_count >= MQTT_BATCH_SIZE) {
            // Durante o delay do batch, enviar heartbeat para manter conexão ativa
            ESP_LOGD(TAG, "Batch complete, applying throttling delay with heartbeat");
            
            // Dividir o delay em chunks menores e enviar heartbeat no meio
            vTaskDelay(pdMS_TO_TICKS(MQTT_BATCH_DELAY_MS / 2));
            
            // Enviar heartbeat no meio do delay para manter conexão
            if (mqtt_connected && xSemaphoreTake(mqtt_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                if (mqtt_client) {
                    int heartbeat_id = esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_STATUS, "OK", 0, 0, 0);
                    if (heartbeat_id >= 0) {
                        last_mqtt_activity_time = xTaskGetTickCount();
                        ESP_LOGD(TAG, "Throttling heartbeat sent during batch delay");
                    }
                }
                xSemaphoreGive(mqtt_mutex);
            }
            
            // Completar o delay restante
            vTaskDelay(pdMS_TO_TICKS(MQTT_BATCH_DELAY_MS / 2));
            mqtt_throttle_reset_batch();
        } else {
            // Delay mínimo para não sobrecarregar o sistema
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

void mqtt_monitor_task(void *pvParameters) {
    ESP_LOGI(TAG, "MQTT monitor task started");
    
    TickType_t last_connection_attempt = 0;
    uint32_t consecutive_failures = 0;
    bool force_recreate = false;
    TickType_t wifi_reconnect_time = 0;

    while (1) {
    EventBits_t wifi_bits = xEventGroupGetBits(wifi_event_group);
    EventBits_t bits = xEventGroupGetBits(system_event_group);
    bool wifi_connected = (wifi_bits & WIFI_CONNECTED_BIT) != 0;
    bool mqtt_connected = (bits & MQTT_CONNECTED_BIT) != 0;

        // Detectar reconexão WiFi recente
        static bool was_wifi_connected = false;
        if (wifi_connected && !was_wifi_connected) {
            wifi_reconnect_time = xTaskGetTickCount();
            force_recreate = true;
            consecutive_failures = 0;
            ESP_LOGI(TAG, "WiFi reconnected, will recreate MQTT client");
        }
        was_wifi_connected = wifi_connected;

        // Se WiFi desconectou, parar tentativas MQTT
        if (!wifi_connected) {
            if (mqtt_connected || consecutive_failures > 0) {
                ESP_LOGW(TAG, "WiFi disconnected, stopping MQTT attempts");
                consecutive_failures = 0;
                force_recreate = true;
                
                if (mqtt_client) {
                    safe_stop_mqtt_client();
                }
            }   
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

    // Se WiFi conectado mas MQTT desconectado (não depende mais de NTP/system_ready)
    if (wifi_connected && !mqtt_connected) {
            TickType_t current_time = xTaskGetTickCount();
            
            // Aguardar mais tempo após reconexão WiFi antes de tentar MQTT
            if (wifi_reconnect_time > 0 && 
                (current_time - wifi_reconnect_time) < pdMS_TO_TICKS(10000)) {
                ESP_LOGD(TAG, "Waiting for WiFi stabilization before MQTT reconnect...");
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }
            
            if (current_time - last_connection_attempt > pdMS_TO_TICKS(MQTT_RECONNECT_DELAY_MS)) {
                ESP_LOGW(TAG, "MQTT disconnected (failures: %d), attempting reconnection...", consecutive_failures);
                
                // Após 2 falhas consecutivas ou reconexão WiFi, recriar cliente MQTT
                if (consecutive_failures >= 2 || force_recreate) {
                    ESP_LOGW(TAG, "Recreating MQTT client (failures: %d, force: %d)...", 
                             consecutive_failures, force_recreate);
                    
                    if (mqtt_client) {
                        safe_stop_mqtt_client();
                        vTaskDelay(pdMS_TO_TICKS(3000)); // Aguardar limpeza
                    }
                    
                    // Aguardar rede estabilizar após WiFi reconnect
                    if (force_recreate) {
                        ESP_LOGI(TAG, "Waiting for network stabilization...");
                        vTaskDelay(pdMS_TO_TICKS(5000));
                    }
                    
                    // Testar DNS com múltiplos servidores
                    ESP_LOGI(TAG, "Testing DNS resolution before MQTT init...");
                    if (test_dns_resolution() != ESP_OK) {
                        ESP_LOGE(TAG, "DNS resolution failed, will retry later");
                        consecutive_failures++;
                        last_connection_attempt = current_time;
                        vTaskDelay(pdMS_TO_TICKS(5000));
                        continue;
                    }
                    
                    mqtt_init();
                    ESP_LOGD(TAG, "After mqtt_init: mqtt_client=%p", mqtt_client);
                    consecutive_failures = 0;
                    force_recreate = false;
                    wifi_reconnect_time = 0; // Reset flag
                } else {
                    // Tentativa simples de reconexão
                    if (mqtt_client) {
                        esp_err_t err = esp_mqtt_client_reconnect(mqtt_client);
                        if (err != ESP_OK) {
                            ESP_LOGE(TAG, "MQTT reconnect failed: %s", esp_err_to_name(err));
                            consecutive_failures++;
                        }
                    } else {
                        ESP_LOGI(TAG, "No MQTT client, creating new one...");
                        if (test_dns_resolution() == ESP_OK) {
                            mqtt_init();
                        } else {
                            consecutive_failures++;
                        }
                    }
                }
                
                last_connection_attempt = current_time;
            }
        }

        // Reset contador de falhas quando conectar com sucesso
        if (mqtt_connected && consecutive_failures > 0) {
            ESP_LOGI(TAG, "MQTT reconnected successfully after %d failures", consecutive_failures);
            consecutive_failures = 0;
            wifi_reconnect_time = 0;
        }

        if (mqtt_connected) {
            static uint32_t last_publish_count = 0;
            static TickType_t last_activity_check = 0;
            TickType_t current_time = xTaskGetTickCount();
    
            // Verificar atividade a cada 2 minutos
            if (current_time - last_activity_check > pdMS_TO_TICKS(120000)) {
                // Se há mensagens pendentes e nenhuma foi enviada recentemente...
                if (mqtt_messages_sent == last_publish_count && ring_idx.count > 0) {
                    ESP_LOGW(TAG, "MQTT appears stalled! Forcing client recreation.");
                    
                    // Força o gatilho de recriação e limpa o bit de conexão
                    force_recreate = true;
                    xEventGroupClearBits(system_event_group, MQTT_CONNECTED_BIT);

                    // Reseta o contador de falhas para iniciar um ciclo de recriação limpo
                    consecutive_failures = 0; 
                    
                    // Pula para a próxima iteração do loop para que a lógica de 
                    // reconexão seja acionada imediatamente
                    last_activity_check = current_time;
                    vTaskDelay(pdMS_TO_TICKS(500)); // Pequeno delay
                    continue; // Força o reprocessamento do loop
                }
                last_publish_count = mqtt_messages_sent;
                last_activity_check = current_time;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10000)); // Check every 10 seconds
    }
}
