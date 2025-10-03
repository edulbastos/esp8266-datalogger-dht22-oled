#include "oled_display.h"
#include "globals.h"
#include "config.h"
#include "ntp_manager.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>
#include "esp_log.h"
#include "ssd1306.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void draw_wifi_icon(void) {
    int x = SCREEN_WIDTH - 14;
    int y = 2;

    ssd1306_drawLine(x+6, y+8, x+8, y+8);
    ssd1306_drawLine(x+4, y+6, x+10, y+6);
    ssd1306_drawLine(x+2, y+4, x+12, y+4);
    ssd1306_putPixel(x+7, y+10);
}

static void clear_wifi_icon(void) {
    int x = SCREEN_WIDTH - 14;
    int y = 2;
    // Limpar área do ícone: ocupa cerca de 14x12 pixels
    ssd1306_setColor(0);
    ssd1306_fillRect(x - 1, y - 1, x + 14, y + 12);
    ssd1306_setColor(1);
}

void draw_degree_symbol(int x, int y) {
    // Desenhar um círculo pequeno de raio 2 pixels para o símbolo °
    ssd1306_putPixel(x+1, y);     // topo
    ssd1306_putPixel(x, y+1);     // esquerda
    ssd1306_putPixel(x+2, y+1);   // direita
    ssd1306_putPixel(x+1, y+2);   // baixo
}

void draw_notify_icon(void) {
    int x = SCREEN_WIDTH  - 12; // Canto inferior direito
    int y = SCREEN_HEIGHT - 25; // Próximo da borda inferior
    
    // Desenhar um sino simples (8x8 pixels)
    // Linha superior do sino
    ssd1306_putPixel(x+3, y);
    ssd1306_putPixel(x+4, y);
    
    // Corpo do sino (formato de sino)
    ssd1306_putPixel(x+2, y+1);
    ssd1306_putPixel(x+5, y+1);
    
    ssd1306_putPixel(x+1, y+2);
    ssd1306_putPixel(x+6, y+2);
    
    ssd1306_putPixel(x+1, y+3);
    ssd1306_putPixel(x+6, y+3);
    
    ssd1306_putPixel(x+1, y+4);
    ssd1306_putPixel(x+6, y+4);
    
    // Base do sino
    ssd1306_putPixel(x, y+5);
    ssd1306_putPixel(x+1, y+5);
    ssd1306_putPixel(x+2, y+5);
    ssd1306_putPixel(x+3, y+5);
    ssd1306_putPixel(x+4, y+5);
    ssd1306_putPixel(x+5, y+5);
    ssd1306_putPixel(x+6, y+5);
    ssd1306_putPixel(x+7, y+5);
    
    // Badalo do sino
    ssd1306_putPixel(x+3, y+6);
    ssd1306_putPixel(x+4, y+6);
}

void clear_notify_icon(void) {
    int x = SCREEN_WIDTH  - 12; // Mesma posição do desenho
    int y = SCREEN_HEIGHT - 25;
    
    // Usar fillRect para desenhar retângulo preto (apagar)
    ssd1306_setColor(0); // Cor preta/apagado
    ssd1306_fillRect(x, y, x + 8, y + 8); // Área do sino (8x8 pixels)
    ssd1306_setColor(1); // Voltar cor branca para outros desenhos
}

void oled_display_task(void *pvParameter) {
    static float prev_temp = -99.0f;
    static float prev_umid = -99.0f;
    static char prev_time_str[64] = "";

    TickType_t last_wake = xTaskGetTickCount();
    
    ssd1306_clearScreen();
    // Estado local do ícone WiFi: -1=unknown,0=off,1=on
    int wifi_icon_state = -1;
    
    while (1) {
        static int last_sync_state = -1;
        int sync_state = is_time_synced() ? 1 : 0;

        if (sync_state != last_sync_state) {
            if (sync_state == 0) {
                ssd1306_setFixedFont(ssd1306xled_font6x8);
                ssd1306_setColor(1);
                ssd1306_printFixed(10, 25, "Inicializando...", STYLE_NORMAL);
            }
            if (sync_state == 1) {
                ssd1306_setColor(0);
                ssd1306_fillRect(10, 25, 10 + 96, 25 + 8);
                ssd1306_setColor(1);
                vTaskDelay(pdMS_TO_TICKS(500)); 
            }
            last_sync_state = sync_state;
        }
        if (sync_state == 0) {
            // Se não sincronizado, apenas mostra mensagem
            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));
            continue;
        }
        // Verifica estado do WiFi e atualiza ícone somente daqui
        if (wifi_event_group != NULL) {
            EventBits_t wbits = xEventGroupGetBits(wifi_event_group);
            int is_connected = (wbits & WIFI_CONNECTED_BIT) ? 1 : 0;
            if (is_connected != wifi_icon_state) {
                if (is_connected) {
                    draw_wifi_icon();
                } else {
                    clear_wifi_icon();
                }
                wifi_icon_state = is_connected;
            }
        }

        // Linha 0: data/hora (sempre limpa e imprime toda a linha, sem flicker)
        time_t now = time(NULL);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        char current_time_str[32];
        snprintf(current_time_str, sizeof(current_time_str), "%02d/%02d/%02d %02d:%02d:%02d",
                 timeinfo.tm_mday, timeinfo.tm_mon + 1, (timeinfo.tm_year + 1900) % 100,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        
        // Atualiza hora se mudou (sem flicker: redesenha por cima, sem limpar)
        if (strcmp(prev_time_str, current_time_str) != 0) {
            // Separar data e hora para atualização independente
            char current_date_part[16] = "";
            char current_time_part[16] = "";
            // Extrair partes da string "DD/MM/YY HH:MM:SS"
            if (strlen(current_time_str) >= 17) {
                strncpy(current_date_part, current_time_str, 8);
                current_date_part[8] = '\0';
                strcpy(current_time_part, current_time_str + 9);
            }
            ssd1306_setFixedFont(ssd1306xled_font6x8);
            // Calcular posições (deslocadas 5 pixels para a esquerda)
            int total_width = strlen(current_time_str) * 6;
            int start_x = (SCREEN_WIDTH - total_width) / 2 - 5;
            int date_width = 8 * 6;
            // Atualiza apenas a data se mudou
            static char prev_date_part[16] = "";
            static char prev_time_part[16] = "";
            if (strcmp(prev_date_part, current_date_part) != 0) {
                ssd1306_printFixed(start_x, 0, current_date_part, STYLE_NORMAL);
                strcpy(prev_date_part, current_date_part);
            }
            // Atualiza apenas a hora se mudou
            if (strcmp(prev_time_part, current_time_part) != 0) {
                int time_x = start_x + date_width + 6;
                ssd1306_printFixed(time_x, 0, current_time_part, STYLE_NORMAL);
                strcpy(prev_time_part, current_time_part);
            }
            strcpy(prev_time_str, current_time_str);
        }

        // Linha 63: xx/yy
        char count_str[40];
    uint32_t xx = mqtt_messages_sent; // Mensagens confirmadas pelo broker (MQTT_EVENT_PUBLISHED)
        uint32_t yy = ring_idx.count;     // Mensagens presentes no backlog
        
        // Formato compacto para números grandes: usa sufixos K, M
        char compact_xx[16], compact_yy[16];
        if (xx >= 1000000) {
            snprintf(compact_xx, sizeof(compact_xx), "%luM", (unsigned long)(xx / 1000000));
        } else if (xx >= 1000) {
            snprintf(compact_xx, sizeof(compact_xx), "%luK", (unsigned long)(xx / 1000));
        } else {
            snprintf(compact_xx, sizeof(compact_xx), "%lu", (unsigned long)xx);
        }
        
        if (yy >= 1000000) {
            snprintf(compact_yy, sizeof(compact_yy), "%luM", (unsigned long)(yy / 1000000));
        } else if (yy >= 1000) {
            snprintf(compact_yy, sizeof(compact_yy), "%luK", (unsigned long)(yy / 1000));
        } else {
            snprintf(compact_yy, sizeof(compact_yy), "%lu", (unsigned long)yy);
        }
        snprintf(count_str, sizeof(count_str), "%s/%s", compact_xx, compact_yy);
        
        // Atualiza o contador se mudou (evita flicker)
        static char prev_count_str[40] = "";
        if (strcmp(prev_count_str, count_str) != 0) {
            int count_width = strlen(count_str) * 6;
            int prev_count_width = strlen(prev_count_str) * 6;
            int max_counter_width = 78;
            int clear_width = (count_width > prev_count_width) ? count_width : prev_count_width;
            if (clear_width > max_counter_width) {
                clear_width = max_counter_width;
            }
            ssd1306_setColor(0);
            ssd1306_fillRect(0, SCREEN_HEIGHT - 8, clear_width + 6, SCREEN_HEIGHT);
            ssd1306_setColor(1);
            ssd1306_setFixedFont(ssd1306xled_font6x8);
            ssd1306_printFixed(0, SCREEN_HEIGHT - 8, count_str, STYLE_NORMAL);
            strcpy(prev_count_str, count_str);
        }

        ssd1306_setFixedFont(ssd1306xled_font6x8);

        // Imprimir versão do firmware no canto inferior direito
        int fw_len = strlen(FIRMWARE_VERSION);
        int fw_x = SCREEN_WIDTH - fw_len * 6;
        ssd1306_printFixed(fw_x, SCREEN_HEIGHT - 8, FIRMWARE_VERSION, STYLE_NORMAL);

        // Imprimir identificação do sensor centralizada na última linha
        int sensor_len = strlen(SENSOR_ID);
        int sensor_x = (SCREEN_WIDTH - sensor_len * 6) / 2;
        ssd1306_printFixed(sensor_x, SCREEN_HEIGHT - 8, SENSOR_ID, STYLE_NORMAL);

        // Obter temperatura e umidade mais recentes
        float current_temp = g_last_temperature;
        float current_umid = g_last_humidity;

        // Atualiza temperatura e umidade se mudaram (sem piscar)
        if (prev_temp != current_temp || prev_umid != current_umid) {

            if ((prev_temp != -99.0f) && (prev_umid != -99.0f)) {
                draw_notify_icon();
                vTaskDelay(250 / portTICK_PERIOD_MS);
                clear_notify_icon();
            }
 
            // Base Y para os dígitos grandes (centralizado verticalmente)
            int y_base = ((SCREEN_HEIGHT - 32) / 2) + 6;                

            // Verifica se sistema está pronto para exibir valores reais
            if (atomic_load(&system_ready)) {
                // Separar partes inteiras e decimais com arredondamento correto (compatível com log)
                int temp_int = (int)current_temp;
                int temp_dec = (int)((current_temp - temp_int) * 10 + 0.5); // Arredondar igual ao log
                int umid_int = (int)current_umid;
                int umid_dec = (int)((current_umid - (float)umid_int) * 10 + 0.5); // Arredondar igual ao log
                
                // Layout centralizado: Temperatura à esquerda, Umidade à direita
                char temp_int_str[4], umid_int_str[4];
                snprintf(temp_int_str, sizeof(temp_int_str), "%d", temp_int);
                snprintf(umid_int_str, sizeof(umid_int_str), "%d", umid_int);              
                
                // Calcular larguras totais para centralização
                int temp_total_width = strlen(temp_int_str) * 16 + 12; // dígitos + coluna vertical (°C/decimal)
                int umid_total_width = strlen(umid_int_str) * 16 + 6;  // dígitos + coluna vertical (%/ponto.decimal)
                int gap = 15; // espaço entre temperatura e umidade
                int combined_width = temp_total_width + gap + umid_total_width;
                int start_x = (SCREEN_WIDTH - combined_width) / 2 - 5;
                

                // TEMPERATURA: dígitos grandes + coluna vertical [°C/ponto.decimal]
                int temp_start_x = start_x;
                ssd1306_setFixedFont(ssd1306xled_font8x16);
                ssd1306_printFixedN(temp_start_x, y_base, temp_int_str, STYLE_NORMAL, FONT_SIZE_2X);              
                // Coluna vertical da temperatura [1,1]=oC [2,1]=ponto.decimal
                ssd1306_setFixedFont(ssd1306xled_font6x8);
                char temp_dec_str[2];
                snprintf(temp_dec_str, sizeof(temp_dec_str), "%d", temp_dec);
                int temp_width = strlen(temp_int_str) * 16; // largura dos dígitos grandes
                int temp_col_x = temp_start_x + temp_width + 2; // coluna à direita dos dígitos
                // [1,1] = "°C" (em cima) - símbolo de graus desenhado + C
                draw_degree_symbol(temp_col_x, y_base + 4);
                ssd1306_printFixed(temp_col_x + 4, y_base + 4, "C", STYLE_NORMAL);
                // [2,1] = ponto + decimal (embaixo, 5 pixels mais próximo do número grande)
                ssd1306_printFixed(temp_col_x - 5, y_base + 24, ".", STYLE_NORMAL);
                ssd1306_printFixed(temp_col_x - 5 + 6, y_base + 24, temp_dec_str, STYLE_NORMAL);

                // UMIDADE: dígitos grandes + coluna vertical [%/ponto.decimal]
                int umid_start_x = temp_start_x + temp_total_width + gap;
                ssd1306_setFixedFont(ssd1306xled_font8x16);
                ssd1306_printFixedN(umid_start_x, y_base, umid_int_str, STYLE_NORMAL, FONT_SIZE_2X);
                // Coluna vertical da umidade [1,1]=% [2,1]=ponto.decimal
                ssd1306_setFixedFont(ssd1306xled_font6x8);
                char umid_dec_str[2];
                snprintf(umid_dec_str, sizeof(umid_dec_str), "%d", umid_dec);
                int umid_width = strlen(umid_int_str) * 16; // largura dos dígitos grandes
                int umid_col_x = umid_start_x + umid_width + 2; // coluna à direita dos dígitos
                // [1,1] = "%" (em cima)
                ssd1306_printFixed(umid_col_x, y_base + 4, "%", STYLE_NORMAL);
                // [2,1] = ponto + decimal (embaixo, 5 pixels mais próximo do número grande)
                ssd1306_printFixed(umid_col_x - 5, y_base + 24, ".", STYLE_NORMAL);
                ssd1306_printFixed(umid_col_x - 5 + 6, y_base + 24, umid_dec_str, STYLE_NORMAL);
            }
            prev_temp = current_temp;
            prev_umid = current_umid;
        }
        vTaskDelayUntil(&last_wake, 1000 / portTICK_PERIOD_MS);
    }
}
