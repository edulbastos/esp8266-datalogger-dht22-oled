#ifndef CONFIG_H
#define CONFIG_H

#include "sdkconfig.h"

#define WIFI_SSID                   CONFIG_WIFI_SSID
#define WIFI_PASS                   CONFIG_WIFI_PASS
#define MQTT_BROKER                 CONFIG_MQTT_BROKER
#define MQTT_USERNAME               CONFIG_MQTT_USERNAME
#define MQTT_PASSWORD               CONFIG_MQTT_PASSWORD
#define MQTT_TOPIC_DATA             CONFIG_MQTT_TOPIC_DATA
#define MQTT_TOPIC_STATUS           CONFIG_MQTT_TOPIC_STATUS
#define MQTT_CLIENT_ID_PREFIX       CONFIG_MQTT_CLIENT_ID_PREFIX
#define SENSOR_ID                   CONFIG_SENSOR_ID
#define MEASUREMENT_INTERVAL_MS     CONFIG_MEASUREMENT_INTERVAL_MS
#define MAX_MEASUREMENTS_BUFFER     CONFIG_MAX_MEASUREMENTS_BUFFER
#define FIRMWARE_VERSION            CONFIG_FIRMWARE_VERSION

// MQTT Batch Configuration (valores conservadores para testes)
#define MQTT_BATCH_SIZE             3       // Reduzir batch para não sobrecarregar broker
#define MQTT_BATCH_DELAY_MS         2000    // Reduzir para 2s para evitar timeout do broker  
#define MQTT_MESSAGE_DELAY_MS       500     // Aguardar 500ms entre mensagens
#define MQTT_RECONNECT_DELAY_MS     CONFIG_MQTT_RECONNECT_DELAY_MS

// SPIFFS Configuration
#define SPIFFS_BASE_PATH            "/spiffs"
#define MEASUREMENTS_FILE           "/spiffs/measurements.dat"
#define INDEX_FILE                  "/spiffs/ring_index.dat"

// NTP Servers (Brasil)
#define NTP_SERVER1                 "200.160.1.186"   // a.st1.ntp.br
#define NTP_SERVER2                 "201.49.148.135"  // b.st1.ntp.br
#define NTP_SERVER3                 "200.186.125.195" // c.st1.ntp.br

// DNS Servers (fallback para Google DNS)
#define DNS_PRIMARY                 "8.8.8.8"   
#define DNS_SECONDARY               "8.8.4.4"     
#define DNS_FALLBACK                "1.1.1.1"  

// Tempo de vida do cache DNS do broker (segundos). Ex: 86400 = 24 horas
#ifdef CONFIG_DNS_CACHE_TTL_SECONDS
#define DNS_CACHE_TTL_SECONDS       CONFIG_DNS_CACHE_TTL_SECONDS
#else
#define DNS_CACHE_TTL_SECONDS       86400
#endif

// Event Bits
#define WIFI_CONNECTED_BIT          BIT0
#define WIFI_FAIL_BIT               BIT1
#define NTP_SYNCED_BIT              BIT0
#define MQTT_CONNECTED_BIT          BIT1
#define SYSTEM_READY_BIT            BIT2

// Quando definido, instruir a task de publish para processar imediatamente o backlog do SPIFFS
#define PROCESS_BACKLOG_BIT         BIT3

// Definições de largura e altura do display
#define SCREEN_WIDTH                 128
#define SCREEN_HEIGHT                64

// Número máximo de mensagens pendentes
#define MAX_PENDING_MSGS            10

// I2C Configuration
#define I2C_MODE_MASTER             0
#define I2C_MASTER_SDA_IO           12
#define I2C_MASTER_SCL_IO           14
#define I2C_OLED_ADDR               0x3C
#define I2C_NUM_0                   0

// DHT22 Configuration
#define DHT22_PIN                   4  // GPIO4

// MQTT Keep-alive Configuration
#define MQTT_KEEPALIVE_SEC          20      // Keep-alive otimizado para estabilidade
#define MQTT_HEARTBEAT_INTERVAL     10      // Heartbeat a cada 10 segundos para manter conexão

// NTP Configuration
#define NTP_SYNC_INTERVAL_SEC       3600    // Ressincronizar a cada 1 hora
#define NTP_SYNC_INTERVAL_SEC_FAST  300     // Ressincronizar a cada 5min se drift detectado  
#define NTP_MAX_DRIFT_SEC           30      // Máximo drift aceitável (30 segundos)
#define NTP_RESYNC_THRESHOLD        86400   // Forçar resync após 24h sem sincronização
#define NTP_CACHE_MAX_AGE           7200    // Cache máximo válido: 2 horas (evitar data antiga)

// Timezone Configuration
#define USE_LOCAL_TIMESTAMP         0       // 0=UTC (recomendado), 1=Local Time (GMT-3)
                                            // UTC é padrão para sistemas distribuídos IoT
                                            
// System Status Configuration (temporariamente desabilitada para testes)
#define SYSTEM_STATUS_ENABLED       0       // 0=Disabled, 1=Enabled
#define SYSTEM_STATUS_INTERVAL_MS   180000  // 3 minutos (reduzido para evitar interferência MQTT)

#endif // CONFIG_H