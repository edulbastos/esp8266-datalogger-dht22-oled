#include "spiffs_manager.h"
#include "globals.h"
#include "config.h"
#include <stdio.h>
#include <string.h>
#include "esp_spiffs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

esp_err_t spiffs_init(void) {
    if (spiffs_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing SPIFFS...");

    esp_vfs_spiffs_conf_t conf = {
        .base_path = SPIFFS_BASE_PATH,
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS: %d KB total, %d KB used", total / 1024, used / 1024);
    }

    spiffs_mutex = xSemaphoreCreateMutex();
    if (spiffs_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create SPIFFS mutex");
        return ESP_FAIL;
    }

    // Carregar índice
    load_spiffs_index();
    spiffs_initialized = true;
    
    ESP_LOGI(TAG, "SPIFFS initialized. Stored measurements: %d", ring_idx.count);
    return ESP_OK;
}

esp_err_t load_spiffs_index(void) {
    FILE* f = fopen(INDEX_FILE, "rb");
    if (f == NULL) {
        ESP_LOGI(TAG, "No index file found, creating new");
        memset(&ring_idx, 0, sizeof(spiffs_ring_index_t));
        return save_spiffs_index();
    }

    size_t read = fread(&ring_idx, sizeof(spiffs_ring_index_t), 1, f);
    fclose(f);

    if (read != 1 || ring_idx.count > MAX_MEASUREMENTS_BUFFER) {
        ESP_LOGW(TAG, "Invalid index, resetting");
        memset(&ring_idx, 0, sizeof(spiffs_ring_index_t));
        return save_spiffs_index();
    }

    return ESP_OK;
}

esp_err_t save_spiffs_index(void) {
    FILE* f = fopen(INDEX_FILE, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open index file for writing");
        return ESP_FAIL;
    }

    size_t written = fwrite(&ring_idx, sizeof(spiffs_ring_index_t), 1, f);
    fclose(f);

    return (written == 1) ? ESP_OK : ESP_FAIL;
}

esp_err_t spiffs_store_measurement(const measurement_data_t* measurement) {
    if (!spiffs_initialized || measurement == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(spiffs_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;

    FILE* f = fopen(MEASUREMENTS_FILE, "r+b");
    if (f == NULL) {
        f = fopen(MEASUREMENTS_FILE, "wb");
        if (f == NULL) {
            ESP_LOGE(TAG, "Failed to create measurements file");
            ret = ESP_FAIL;
            goto cleanup;
        }
    }

    // Posicionar no head
    if (fseek(f, ring_idx.head * sizeof(measurement_data_t), SEEK_SET) != 0) {
        ESP_LOGE(TAG, "Failed to seek file position");
        ret = ESP_FAIL;
        goto cleanup;
    }

    /*
     * Before writing, normalize timestamp if it appears to be uptime-based
     * and system time is synced: convert uptime seconds/ms to epoch seconds.
     */
    measurement_data_t local = *measurement; // local copy we can modify
    uint32_t raw_ts = measurement->timestamp;
    uint32_t ts = raw_ts;
    uint32_t uptime_s = (xTaskGetTickCount() * portTICK_PERIOD_MS) / 1000U;
    time_t now = time(NULL);

    if (ts != 0 && time_synced) {
        if (ts <= uptime_s + 60U) {
            // timestamp looks like uptime in seconds
            uint32_t delta = uptime_s - ts;
            uint32_t new_ts = (uint32_t)now - delta;
            ESP_LOGI(TAG, "Normalizing stored timestamp from uptime_s=%u to epoch=%u", ts, new_ts);
            local.timestamp = new_ts;
        } else if (ts > uptime_s * 1000U && (ts / 1000U) <= uptime_s) {
            // timestamp looks like uptime in milliseconds
            uint32_t stored_uptime_s = ts / 1000U;
            uint32_t delta = uptime_s - stored_uptime_s;
            uint32_t new_ts = (uint32_t)now - delta;
            ESP_LOGI(TAG, "Normalizing stored timestamp from uptime_ms=%u to epoch=%u", ts, new_ts);
            local.timestamp = new_ts;
        }
    }

    // Escrever medição (usando cópia local possivelmente normalizada)
    if (fwrite(&local, sizeof(measurement_data_t), 1, f) != 1) {
        ESP_LOGE(TAG, "Failed to write measurement");
        ret = ESP_FAIL;
        goto cleanup;
    }

    fclose(f);
    f = NULL;

    // Atualizar índices
    ring_idx.head = (ring_idx.head + 1) % MAX_MEASUREMENTS_BUFFER;
    ring_idx.total_written++;

    if (ring_idx.count < MAX_MEASUREMENTS_BUFFER) {
        ring_idx.count++;
    } else {
        ring_idx.tail = (ring_idx.tail + 1) % MAX_MEASUREMENTS_BUFFER;
    }

    ret = save_spiffs_index();
    
    ESP_LOGI(TAG, "Stored measurement in SPIFFS. Buffer: %d/%d", 
             ring_idx.count, MAX_MEASUREMENTS_BUFFER);

cleanup:
    if (f != NULL) {
        fclose(f);
    }
    xSemaphoreGive(spiffs_mutex);
    return ret;
}

esp_err_t spiffs_get_next_measurement(measurement_data_t* measurement) {
    if (!spiffs_initialized || measurement == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(spiffs_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;

    if (ring_idx.count == 0) {
        ret = ESP_ERR_NOT_FOUND;
        goto cleanup;
    }

    FILE* f = fopen(MEASUREMENTS_FILE, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open measurements file");
        ret = ESP_FAIL;
        goto cleanup;
    }

    if (fseek(f, ring_idx.tail * sizeof(measurement_data_t), SEEK_SET) != 0) {
        ESP_LOGE(TAG, "Failed to seek file position");
        ret = ESP_FAIL;
        goto cleanup_file;
    }

    if (fread(measurement, sizeof(measurement_data_t), 1, f) != 1) {
        ESP_LOGE(TAG, "Failed to read measurement");
        ret = ESP_FAIL;
        goto cleanup_file;
    }

cleanup_file:
    fclose(f);
cleanup:
    xSemaphoreGive(spiffs_mutex);
    return ret;
}

esp_err_t spiffs_get_and_remove_next_measurement(measurement_data_t* measurement) {
    if (!spiffs_initialized || measurement == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(spiffs_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;

    if (ring_idx.count == 0) {
        ret = ESP_ERR_NOT_FOUND;
        goto cleanup;
    }

    FILE* f = fopen(MEASUREMENTS_FILE, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open measurements file");
        ret = ESP_FAIL;
        goto cleanup;
    }

    if (fseek(f, ring_idx.tail * sizeof(measurement_data_t), SEEK_SET) != 0) {
        ESP_LOGE(TAG, "Failed to seek file position");
        ret = ESP_FAIL;
        goto cleanup_file;
    }

    if (fread(measurement, sizeof(measurement_data_t), 1, f) != 1) {
        ESP_LOGE(TAG, "Failed to read measurement");
        ret = ESP_FAIL;
        goto cleanup_file;
    }

    // Remover imediatamente após leitura bem-sucedida
    ring_idx.tail = (ring_idx.tail + 1) % MAX_MEASUREMENTS_BUFFER;
    ring_idx.count--;

    esp_err_t save_ret = save_spiffs_index();
    if (save_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save SPIFFS index after removal");
        // Não falhar a operação por causa disso, apenas avisar
    }
    
    ESP_LOGI(TAG, "Got and removed measurement ID %u. Remaining: %d", 
             measurement->measurement_id, ring_idx.count);

cleanup_file:
    fclose(f);
cleanup:
    xSemaphoreGive(spiffs_mutex);
    return ret;
}

esp_err_t spiffs_rollback_measurement(const measurement_data_t* measurement) {
    if (!spiffs_initialized || measurement == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(spiffs_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;

    // Verificar se há espaço (não deveria estar cheio, mas por segurança)
    if (ring_idx.count >= MAX_MEASUREMENTS_BUFFER) {
        ESP_LOGW(TAG, "Cannot rollback: SPIFFS buffer is full");
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    FILE* f = fopen(MEASUREMENTS_FILE, "r+b");
    if (f == NULL) {
        f = fopen(MEASUREMENTS_FILE, "wb");
        if (f == NULL) {
            ESP_LOGE(TAG, "Failed to open measurements file for rollback");
            ret = ESP_FAIL;
            goto cleanup;
        }
    }

    // Recuar o tail para re-criar o espaço
    ring_idx.tail = (ring_idx.tail - 1 + MAX_MEASUREMENTS_BUFFER) % MAX_MEASUREMENTS_BUFFER;
    ring_idx.count++;

    // Escrever a medição de volta na posição tail
    if (fseek(f, ring_idx.tail * sizeof(measurement_data_t), SEEK_SET) != 0) {
        ESP_LOGE(TAG, "Failed to seek file position for rollback");
        ret = ESP_FAIL;
        goto cleanup_file;
    }

    if (fwrite(measurement, sizeof(measurement_data_t), 1, f) != 1) {
        ESP_LOGE(TAG, "Failed to write measurement for rollback");
        ret = ESP_FAIL;
        goto cleanup_file;
    }

    fflush(f);

    esp_err_t save_ret = save_spiffs_index();
    if (save_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save SPIFFS index after rollback");
        ret = ESP_FAIL;
    } else {
        ESP_LOGI(TAG, "Rolled back measurement ID %u. Total: %d", 
                 measurement->measurement_id, ring_idx.count);
    }

cleanup_file:
    fclose(f);
cleanup:
    xSemaphoreGive(spiffs_mutex);
    return ret;
}

esp_err_t spiffs_remove_sent_measurement(void) {
    if (!spiffs_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(spiffs_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;

    if (ring_idx.count == 0) {
        ret = ESP_ERR_NOT_FOUND;
        goto cleanup;
    }

    ring_idx.tail = (ring_idx.tail + 1) % MAX_MEASUREMENTS_BUFFER;
    ring_idx.count--;

    ret = save_spiffs_index();
    
    ESP_LOGI(TAG, "Removed sent measurement. Remaining: %d", ring_idx.count);

cleanup:
    xSemaphoreGive(spiffs_mutex);
    return ret;
}

void spiffs_print_status(void) {
    ESP_LOGI(TAG, "=== SPIFFS Status ===");
    ESP_LOGI(TAG, "Stored measurements: %d/%d", ring_idx.count, MAX_MEASUREMENTS_BUFFER);
    ESP_LOGI(TAG, "Total written: %d", ring_idx.total_written);
    ESP_LOGI(TAG, "Head: %d, Tail: %d", ring_idx.head, ring_idx.tail);
    
    if (ring_idx.count >= MAX_MEASUREMENTS_BUFFER * 0.8) {
        ESP_LOGW(TAG, "SPIFFS buffer is %d%% full!", 
                 (ring_idx.count * 100) / MAX_MEASUREMENTS_BUFFER);
    }
}
