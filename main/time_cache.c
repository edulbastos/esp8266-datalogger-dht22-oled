#include "time_cache.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "TIME_CACHE";
#define NVS_NAMESPACE "time_cache"
#define NVS_KEY_TIME "cached_time"

esp_err_t time_cache_save(time_t t) {
    nvs_handle handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    uint32_t v = (uint32_t)t;
    err = nvs_set_u32(handle, NVS_KEY_TIME, v);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err == ESP_OK) ESP_LOGI(TAG, "Saved cached time: %u", v);
    return err;
}

esp_err_t time_cache_load(time_t *out) {
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    nvs_handle handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    uint32_t v = 0;
    err = nvs_get_u32(handle, NVS_KEY_TIME, &v);
    nvs_close(handle);
    if (err == ESP_OK) {
        *out = (time_t)v;
        ESP_LOGI(TAG, "Loaded cached time: %u", v);
    }
    return err;
}

esp_err_t time_cache_clear(void) {
    nvs_handle handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_erase_key(handle, NVS_KEY_TIME);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
