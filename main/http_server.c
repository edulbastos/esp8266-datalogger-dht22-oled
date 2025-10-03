#include "http_server.h"
#include "globals.h"
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "lwip/api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Verifica se o request começa com o path especificado
static bool request_starts_with(const char *buf, const char *path) {
    // Formato: "GET /path HTTP/1.1"
    char expected[64];
    snprintf(expected, sizeof(expected), "GET %s ", path);
    return strncmp(buf, expected, strlen(expected)) == 0;
}

void http_server_task(void *pvParameters) {
    ESP_LOGI(TAG, "HTTP server task started");
    struct netconn *conn, *newconn;
    conn = netconn_new(NETCONN_TCP);
    netconn_bind(conn, NULL, 80);
    netconn_listen(conn);

    while (1) {
        if (netconn_accept(conn, &newconn) == ERR_OK) {
            struct netbuf *inbuf;
            char *buf;
            u16_t buflen;

            if (netconn_recv(newconn, &inbuf) == ERR_OK && inbuf != NULL) {
                netbuf_data(inbuf, (void**)&buf, &buflen);

                // Endpoint: GET /data (JSON com última medição)
                if (request_starts_with(buf, "/data")) {
                    const char *hdr =
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: application/json\r\n"
                        "Connection: close\r\n"
                        "Cache-Control: no-store\r\n\r\n";
                    netconn_write(newconn, hdr, strlen(hdr), NETCONN_COPY);

                    char json[384];
                    snprintf(json, sizeof(json),
                             "{\"sensor_id\":\"%s\",\"timestamp\":%lu,\"temperature\":%.1f,\"humidity\":%.1f}",
                             last_measurement.sensor_id,
                             (unsigned long)last_measurement.timestamp,
                             last_measurement.temperature,
                             last_measurement.humidity);
                    netconn_write(newconn, json, strlen(json), NETCONN_COPY);
                }
                // Endpoint: GET /status (JSON com status completo do sistema)
                else if (request_starts_with(buf, "/status")) {
                    const char *hdr =
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: application/json\r\n"
                        "Connection: close\r\n"
                        "Cache-Control: no-store\r\n\r\n";
                    netconn_write(newconn, hdr, strlen(hdr), NETCONN_COPY);

                    char mac_str[20];
                    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                        last_measurement.mac_address[0], last_measurement.mac_address[1],
                        last_measurement.mac_address[2], last_measurement.mac_address[3],
                        last_measurement.mac_address[4], last_measurement.mac_address[5]);

                    bool wifi_connected = false;
                    bool mqtt_connected = false;
                    if (wifi_event_group != NULL) {
                        EventBits_t bits = xEventGroupGetBits(wifi_event_group);
                        wifi_connected = (bits & WIFI_CONNECTED_BIT) != 0;
                    }
                    if (system_event_group != NULL) {
                        EventBits_t bits = xEventGroupGetBits(system_event_group);
                        mqtt_connected = (bits & MQTT_CONNECTED_BIT) != 0;
                    }

                    char json[512];
                    snprintf(json, sizeof(json),
                             "{\"firmware\":\"%s\",\"sensor_id\":\"%s\",\"mac\":\"%s\","
                             "\"wifi_connected\":%s,\"mqtt_connected\":%s,"
                             "\"mqtt_sent\":%lu,\"backlog_count\":%lu,"
                             "\"last_measurement\":{\"timestamp\":%lu,\"temperature\":%.1f,\"humidity\":%.1f}}",
                             FIRMWARE_VERSION,
                             last_measurement.sensor_id,
                             mac_str,
                             wifi_connected ? "true" : "false",
                             mqtt_connected ? "true" : "false",
                             (unsigned long)mqtt_messages_sent,
                             (unsigned long)ring_idx.count,
                             (unsigned long)last_measurement.timestamp,
                             last_measurement.temperature,
                             last_measurement.humidity);
                    netconn_write(newconn, json, strlen(json), NETCONN_COPY);
                }
                // Endpoint: GET / (página HTML principal)
                else {
                    const char *hdr =
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/html; charset=UTF-8\r\n"
                        "Connection: close\r\n"
                        "Cache-Control: no-store\r\n\r\n";
                    netconn_write(newconn, hdr, strlen(hdr), NETCONN_COPY);

                    // Início do HTML
                    const char *html_start =
                        "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
                        "<meta charset='UTF-8'>"
                        "<meta http-equiv='refresh' content='1'>"
                        "<style>body{font-family:sans-serif;background:#f4f4f4;margin:0;padding:0;}"
                        ".container{max-width:400px;margin:40px auto;background:#fff;padding:24px;"
                        "border-radius:8px;box-shadow:0 2px 8px #ccc;}"
                        "h1{color:#2196F3;} .data{font-size:1.2em;margin:12px 0;"
                        "display:flex;justify-content:space-between;} .label{color:#888;}"
                        "@media(max-width:500px){.container{margin:10px;padding:10px;}}</style></head><body>"
                        "<div class='container'><h1>ESP8266 Datalogger</h1>";
                    netconn_write(newconn, html_start, strlen(html_start), NETCONN_COPY);

                    // Dados dinâmicos (temperatura, umidade, etc.)
                    char line[192];
                    struct tm timeinfo;
                    localtime_r((time_t*)&last_measurement.timestamp, &timeinfo);
                    char date_str[64];
                    strftime(date_str, sizeof(date_str), "%d/%m/%Y %H:%M:%S", &timeinfo);

                    char mac_str[20];
                    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                        last_measurement.mac_address[0], last_measurement.mac_address[1],
                        last_measurement.mac_address[2], last_measurement.mac_address[3],
                        last_measurement.mac_address[4], last_measurement.mac_address[5]);

                    snprintf(line, sizeof(line),
                             "<div class='data'><span class='label'>Temperatura:</span><span>%.1f°C</span></div>",
                             last_measurement.temperature);
                    netconn_write(newconn, line, strlen(line), NETCONN_COPY);

                    snprintf(line, sizeof(line),
                             "<div class='data'><span class='label'>Umidade:</span><span>%.1f%%</span></div>",
                             last_measurement.humidity);
                    netconn_write(newconn, line, strlen(line), NETCONN_COPY);

                    snprintf(line, sizeof(line),
                             "<div class='data'><span class='label'>Data da Medição:</span><span>%s</span></div>",
                             date_str);
                    netconn_write(newconn, line, strlen(line), NETCONN_COPY);

                    snprintf(line, sizeof(line),
                             "<div class='data'><span class='label'>MAC:</span><span>%s</span></div>",
                             mac_str);
                    netconn_write(newconn, line, strlen(line), NETCONN_COPY);

                    snprintf(line, sizeof(line),
                             "<div class='data'><span class='label'>Firmware:</span><span>%s</span></div>",
                             FIRMWARE_VERSION);
                    netconn_write(newconn, line, strlen(line), NETCONN_COPY);

                    snprintf(line, sizeof(line),
                             "<div class='data'><span class='label'>Sensor ID:</span><span>%s</span></div>",
                             last_measurement.sensor_id);
                    netconn_write(newconn, line, strlen(line), NETCONN_COPY);

                    // Finaliza HTML
                    const char *html_end = "</div></body></html>";
                    netconn_write(newconn, html_end, strlen(html_end), NETCONN_COPY);
                }

                netbuf_delete(inbuf);
            }
            netconn_close(newconn);
            netconn_delete(newconn);
        }
    }
}
