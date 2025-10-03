#ifndef TIME_CACHE_H
#define TIME_CACHE_H

#include "esp_err.h"
#include <time.h>

// Save the given epoch seconds to NVS
esp_err_t time_cache_save(time_t t);

// Load cached epoch seconds from NVS; returns ESP_OK if found
esp_err_t time_cache_load(time_t *out);

// Clear cached time
esp_err_t time_cache_clear(void);

#endif // TIME_CACHE_H
