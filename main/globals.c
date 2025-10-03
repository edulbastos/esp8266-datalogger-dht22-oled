#include "globals.h"
#include <stdatomic.h>

// Definição das variáveis globais

const char *TAG = "DATALOGGER";

// Variáveis globais para última medição
float g_last_temperature = -99.0f;
float g_last_humidity = -99.0f;

// Event Groups e Queues
EventGroupHandle_t wifi_event_group = NULL;
EventGroupHandle_t system_event_group = NULL;
QueueHandle_t measurement_queue = NULL;

// Handles
esp_mqtt_client_handle_t mqtt_client = NULL;
SemaphoreHandle_t spiffs_mutex = NULL;
SemaphoreHandle_t oled_mutex = NULL;
SemaphoreHandle_t mqtt_mutex = NULL;

// MQTT client ID único baseado em MAC + timestamp
char mqtt_client_id[40] = {0};
char mqtt_broker_ip[16] = {0};

// MQTT Throttling
uint32_t mqtt_messages_sent = 0;
uint32_t mqtt_batch_count = 0;
TickType_t last_batch_time = 0;
uint32_t mqtt_publish_attempts = 0;

// Sistema de arquivos
spiffs_ring_index_t ring_idx = {0};
bool spiffs_initialized = false;

// Estado do sistema
system_state_t current_state = SYSTEM_INIT;
int wifi_retry_num = 0;
bool time_synced = false;
uint32_t measurement_counter = 0;
atomic_bool system_ready = ATOMIC_VAR_INIT(false);

// Estrutura para rastrear mensagens pendentes de confirmação MQTT
mqtt_pending_t mqtt_pending_msgs[MAX_PENDING_MSGS];
int mqtt_pending_count = 0;

measurement_data_t last_measurement = {0};
