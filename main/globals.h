#ifndef GLOBALS_H
#define GLOBALS_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "mqtt_client.h"
#include "types.h"
#include "config.h"
#include <stdatomic.h>

// ======================= VARIÁVEIS GLOBAIS =======================

extern const char *TAG;

// Variáveis globais para última medição
extern float g_last_temperature;
extern float g_last_humidity;

// Event Groups e Queues
extern EventGroupHandle_t wifi_event_group;
extern EventGroupHandle_t system_event_group;
extern QueueHandle_t measurement_queue;

// Handles
extern esp_mqtt_client_handle_t mqtt_client;
extern SemaphoreHandle_t spiffs_mutex;
extern SemaphoreHandle_t oled_mutex;
extern SemaphoreHandle_t mqtt_mutex;

// MQTT client ID único baseado em MAC + timestamp
extern char mqtt_client_id[40];
extern char mqtt_broker_ip[16];

// MQTT Throttling
extern uint32_t mqtt_messages_sent;
extern uint32_t mqtt_batch_count;
extern TickType_t last_batch_time;
extern uint32_t mqtt_publish_attempts; // number of esp_mqtt_client_publish attempts that returned msg_id>=0

// Sistema de arquivos
extern spiffs_ring_index_t ring_idx;
extern bool spiffs_initialized;

// Estado do sistema
extern system_state_t current_state;
extern int wifi_retry_num;
extern bool time_synced;
extern uint32_t measurement_counter;
extern atomic_bool system_ready;

// Estrutura para rastrear mensagens pendentes de confirmação MQTT
extern mqtt_pending_t mqtt_pending_msgs[MAX_PENDING_MSGS];
extern int mqtt_pending_count;

extern measurement_data_t last_measurement;

#endif // GLOBALS_H
