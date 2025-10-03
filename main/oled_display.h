#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H



/**
 * @brief Função para desenhar o símbolo de grau
 * @param x Coordenada X
 * @param y Coordenada Y
 */
void draw_degree_symbol(int x, int y);

/**
 * @brief Função para desenhar o ícone de notificação
 */
void draw_notify_icon(void);

/**
 * @brief Função para apagar o ícone de notificação
 */
void clear_notify_icon(void);

/**
 * @brief Tarefa para atualizar o display OLED com temperatura e umidade
 * @param pvParameter Parâmetros da task (não utilizado)
 */
void oled_display_task(void *pvParameter);

#endif // OLED_DISPLAY_H
