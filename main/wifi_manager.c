#include "wifi_manager.h"
#include "globals.h"
#include "config.h"
#include "dns_manager.h"
#include "ntp_manager.h"
#include "oled_display.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "tcpip_adapter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

// WiFi event handler
void wifi_event_handler(void* arg, esp_event_base_t event_base,
                        int32_t event_id, void* event_data)
{
    // Central handler for WiFi/IP events. Keep cases small and return early.
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                // Station interface started; connection is handled elsewhere.
                ESP_LOGD(TAG, "WIFI_EVENT_STA_START");
                return;

            case WIFI_EVENT_STA_DISCONNECTED:
                // Clear connected bit and mark state so reconnect manager runs.
                ESP_LOGI(TAG, "WIFI_EVENT_STA_DISCONNECTED");
                xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
                current_state = WIFI_CONNECTING;
                ESP_LOGI(TAG, "WiFi disconnected, waiting for reconnect manager task");
                return;

            default:
                ESP_LOGD(TAG, "Unhandled WIFI_EVENT id=%d", event_id);
                return;
        }
    }

    if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* got_ip = (ip_event_got_ip_t*) event_data;
            if (got_ip) {
                ESP_LOGI(TAG, "IP acquired: " IPSTR, IP2STR(&got_ip->ip_info.ip));
            } else {
                ESP_LOGI(TAG, "IP acquired (no details)");
            }
            // Reset retry counters and mark connected
            wifi_retry_num = 0;
            current_state = WIFI_CONNECTED;
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
            // Call DNS resolution now that DHCP-provided DNS is available
            if (test_dns_resolution() != ESP_OK) {
                ESP_LOGW(TAG, "DNS resolution test failed on IP event, but continuing...");
            }
            // Initialize NTP after IP obtained
            ntp_init();
            return;
        }
        ESP_LOGD(TAG, "Unhandled IP_EVENT id=%d", event_id);
        return;
    }

    ESP_LOGD(TAG, "Unhandled event base: %s", event_base);
}


// Initialize WiFi in station mode
esp_err_t wifi_init_sta(void) {

    /* Create event group used to signal when we are connected */
    wifi_event_group = xEventGroupCreate();
    if (wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create wifi_event_group");
        return ESP_FAIL;
    }

    /* Initialize TCP/IP stack */
    tcpip_adapter_init();

    ESP_LOGI(TAG, "Initializing WiFi (station)...");

    /* Create default event loop and initialize WiFi driver */
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Register event handlers for WiFi and IP events */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,
                                               ESP_EVENT_ANY_ID,
                                               &wifi_event_handler,
                                               NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,
                                               IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler,
                                               NULL));

    /* Prepare WiFi configuration (copy SSID/password safely) */
    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    /* Set mode, apply config and start WiFi */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi initialization finished. Connecting...");
    current_state = WIFI_CONNECTING;
    return ESP_OK;
}

// WiFi monitor task
void wifi_monitor_task(void *pvParameters) {
    ESP_LOGI(TAG, "WiFi monitor task started");

    while (1) {
        EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                               WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                               false,
                                               false,
                                               pdMS_TO_TICKS(10000));

        if (bits & WIFI_CONNECTED_BIT) {

            ESP_LOGI(TAG, "Connected to WiFi successfully");

        } else if (bits & WIFI_FAIL_BIT) {
            ESP_LOGI(TAG, "Failed to connect to WiFi");
            current_state = SYSTEM_ERROR;
        } else {
            ESP_LOGI(TAG, "WiFi connection timeout");
        }

        vTaskDelay(pdMS_TO_TICKS(30000)); // Check every 30 seconds
    }
}


// WiFi reconnect manager task
void wifi_reconnect_manager_task(void *pvParameters) {
    
    ESP_LOGI(TAG, "WiFi reconnect manager task started");

    uint32_t reconnect_interval = 5000; // 5s inicial
    const uint32_t max_interval = 300000; // 5min máximo
    uint32_t failed_attempts = 0;

    while (1) {
        EventBits_t wifi_bits = xEventGroupGetBits(wifi_event_group);
        bool wifi_connected = (wifi_bits & WIFI_CONNECTED_BIT) != 0;

        if (wifi_connected) {
            // Resetar contadores ao conectar
            failed_attempts = 0;
            reconnect_interval = 5000;
            vTaskDelay(pdMS_TO_TICKS(30000)); // Checa a cada 30s
            continue;
        }

        // WiFi desconectado, tentar reconectar
        ESP_LOGW(TAG, "WiFi disconnected, reconnect attempt #%d (interval: %d ms)", failed_attempts + 1, reconnect_interval);
        esp_wifi_connect();
        failed_attempts++;

        // Backoff exponencial
        if (failed_attempts % 3 == 0) {
            reconnect_interval *= 2;
            if (reconnect_interval > max_interval) reconnect_interval = max_interval;
        }

        // Reboot após 20 falhas
        if (failed_attempts >= 20) {
            ESP_LOGE(TAG, "WiFi reconnect failed 20 times. Rebooting ESP...");
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_restart();
        }

        vTaskDelay(pdMS_TO_TICKS(reconnect_interval));
    }
}
