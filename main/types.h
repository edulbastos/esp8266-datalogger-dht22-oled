#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stdbool.h>

// Estrutura de medição
typedef struct {
    uint32_t timestamp;
    char sensor_id[16];
    uint8_t mac_address[6];
    float temperature;
    float humidity;
    uint8_t retry_count;
    uint32_t measurement_id;
} measurement_data_t;

// Estrutura para rastrear mensagens pendentes de confirmação MQTT
typedef struct {
    int msg_id;
    measurement_data_t measurement;
    bool is_stored; // true se veio da SPIFFS
} mqtt_pending_t;

// Estrutura do índice do ringbuffer SPIFFS
typedef struct {
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint32_t total_written;
} spiffs_ring_index_t;

// Estados do sistema
typedef enum {
    SYSTEM_INIT,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    NTP_SYNCING,
    NTP_SYNCED,
    MQTT_CONNECTING,
    MQTT_CONNECTED,
    SYSTEM_READY,
    SYSTEM_ERROR
} system_state_t;

#endif // TYPES_H
