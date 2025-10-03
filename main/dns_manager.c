#include "dns_manager.h"
#include "globals.h"
#include "config.h"
#include <string.h>
#include "esp_log.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
// NVS and sockets for caching and quick reachability checks
#include "nvs.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>
#include <time.h>

// forward declaration for internal helper
static bool dns_check_ip_reachable(const char* ip, uint16_t port, uint32_t timeout_ms);

// Configurar servidores DNS
void configure_dns_servers(void) {
    // Configurar DNS servers alternativos
    ip_addr_t dns_primary, dns_secondary;
    
    // Configurar DNS primário (Google)
    ip4addr_aton(DNS_PRIMARY, &dns_primary);
    dns_setserver(0, &dns_primary);
    ESP_LOGI(TAG, "DNS Primary set to: %s", DNS_PRIMARY);
    
    // Configurar DNS secundário (Google)
    ip4addr_aton(DNS_SECONDARY, &dns_secondary);
    dns_setserver(1, &dns_secondary);
    ESP_LOGI(TAG, "DNS Secondary set to: %s", DNS_SECONDARY);
    
    ESP_LOGI(TAG, "DNS servers configured successfully");
}

// Testar resolução DNS
esp_err_t test_dns_resolution(void) {
    ESP_LOGI(TAG, "Testing DNS resolution...");
    
    // Extrair hostname do broker MQTT URL
    char hostname[128];
    const char* broker_url = MQTT_BROKER;
    
    // Remover "mqtt://" do início
    const char* host_start = strstr(broker_url, "://");
    if (host_start != NULL) {
        host_start += 3; // Pular "://"
        
        // Copiar hostname até encontrar ':' ou fim da string
        const char* port_start = strchr(host_start, ':');
        size_t hostname_len;
        
        if (port_start != NULL) {
            hostname_len = port_start - host_start;
        } else {
            hostname_len = strlen(host_start);
        }
        
        if (hostname_len < sizeof(hostname)) {
            strncpy(hostname, host_start, hostname_len);
            hostname[hostname_len] = '\0';
        } else {
            ESP_LOGE(TAG, "Hostname too long");
            return ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "Invalid broker URL format");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Attempting to resolve: %s", hostname);
    
    // 1) Tentar resolver com o resolvedor do sistema (DHCP-provided DNS)
    ESP_LOGI(TAG, "Trying system resolver (DHCP-provided DNS)");
    struct hostent *he = gethostbyname(hostname);
    if (he != NULL) {
        struct in_addr addr;
        addr.s_addr = *((unsigned long *)he->h_addr_list[0]);
        strcpy(mqtt_broker_ip, inet_ntoa(addr));
        ESP_LOGI(TAG, "System DNS resolution successful: %s -> %s", hostname, mqtt_broker_ip);
        // Salvar IP resolvido em NVS
        esp_err_t save_err = dns_save_cached_broker_ip(mqtt_broker_ip);
        if (save_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to save broker IP to NVS: %s", esp_err_to_name(save_err));
        }
        return ESP_OK;
    }

    // 2) Se falhou, tentar servidores fallback (Google / configurados)
    ESP_LOGW(TAG, "System resolver failed, trying fallback DNS servers");
    const char* dns_servers[] = {DNS_PRIMARY, DNS_SECONDARY, DNS_FALLBACK, "208.67.222.222"}; // OpenDNS
    int dns_count = sizeof(dns_servers) / sizeof(dns_servers[0]);

    // Save current DNS servers to restore later
    ip_addr_t orig_dns0;
    ip_addr_t orig_dns1;
    const ip_addr_t *p0 = dns_getserver(0);
    const ip_addr_t *p1 = dns_getserver(1);
    if (p0) orig_dns0 = *p0; else { IP4_ADDR(&orig_dns0, 0,0,0,0); }
    if (p1) orig_dns1 = *p1; else { IP4_ADDR(&orig_dns1, 0,0,0,0); }

    for (int dns_idx = 0; dns_idx < dns_count; dns_idx++) {
        ESP_LOGI(TAG, "Trying DNS server [%d/%d]: %s", dns_idx + 1, dns_count, dns_servers[dns_idx]);
        ip_addr_t dns_addr;
        if (ip4addr_aton(dns_servers[dns_idx], &dns_addr) == 0) {
            ESP_LOGE(TAG, "Invalid DNS server IP: %s", dns_servers[dns_idx]);
            continue;
        }
        dns_setserver(0, &dns_addr);
        dns_setserver(1, &dns_addr);
        vTaskDelay(pdMS_TO_TICKS(1500));

        he = gethostbyname(hostname);
        if (he != NULL) {
            struct in_addr addr;
            addr.s_addr = *((unsigned long *)he->h_addr_list[0]);
            strcpy(mqtt_broker_ip, inet_ntoa(addr));
            ESP_LOGI(TAG, "Fallback DNS resolution successful with %s: %s -> %s", dns_servers[dns_idx], hostname, mqtt_broker_ip);
            esp_err_t save_err = dns_save_cached_broker_ip(mqtt_broker_ip);
            if (save_err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to save broker IP to NVS: %s", esp_err_to_name(save_err));
            }
            // restore original DNS servers
            dns_setserver(0, &orig_dns0);
            dns_setserver(1, &orig_dns1);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "Fallback DNS failed with %s (h_errno: %d)", dns_servers[dns_idx], h_errno);
    }

    // restore original DNS servers
    dns_setserver(0, &orig_dns0);
    dns_setserver(1, &orig_dns1);

    // 3) Último recurso: usar IP cacheado em NVS se existir
    esp_err_t nvs_err = dns_load_cached_broker_ip();
    if (nvs_err == ESP_OK && strlen(mqtt_broker_ip) > 0) {
        ESP_LOGI(TAG, "Using cached broker IP from NVS as last resort: %s — testing reachability", mqtt_broker_ip);
        if (dns_check_ip_reachable(mqtt_broker_ip, 1883, 500)) {
            ESP_LOGI(TAG, "Cached broker IP is reachable, using it");
            return ESP_OK;
        } else {
            ESP_LOGW(TAG, "Cached broker IP not reachable");
        }
    } else if (nvs_err != ESP_ERR_NVS_NOT_FOUND && nvs_err != ESP_OK) {
        ESP_LOGW(TAG, "NVS read error when loading cached broker IP: %s", esp_err_to_name(nvs_err));
    }

    ESP_LOGE(TAG, "DNS resolution failed with system and fallback servers, and no usable cached IP");
    return ESP_FAIL;
}

// NVS namespace/keys (IP + timestamp)
#define DNS_NVS_NAMESPACE "dns_cache"
#define DNS_NVS_KEY_BROKER_IP "broker_ip"
#define DNS_NVS_KEY_BROKER_TS "broker_ip_ts"

// Load cached IP and validate TTL. If expired, clear and return ESP_ERR_NVS_NOT_FOUND
esp_err_t dns_load_cached_broker_ip(void) {
    nvs_handle handle;
    esp_err_t err = nvs_open(DNS_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;

    // Read ip
    size_t required = sizeof(mqtt_broker_ip);
    err = nvs_get_str(handle, DNS_NVS_KEY_BROKER_IP, mqtt_broker_ip, &required);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    // Read timestamp (uint32)
    uint32_t saved_ts = 0;
    err = nvs_get_u32(handle, DNS_NVS_KEY_BROKER_TS, &saved_ts);
    nvs_close(handle);

    // If we couldn't read timestamp, accept the cached IP (backward compat)
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        // Non-fatal, keep IP
        return ESP_OK;
    }

    // If timestamp exists, and time is available, check TTL
    if (saved_ts != 0) {
        time_t now = time(NULL);
        if (now > 100000) { // crude check time synced (> 1970 + margin)
            if ((uint32_t)now - saved_ts > (uint32_t)DNS_CACHE_TTL_SECONDS) {
                ESP_LOGI(TAG, "Cached broker IP expired (saved=%u, now=%u), clearing", saved_ts, (uint32_t)now);
                // clear cache
                dns_clear_cached_broker_ip();
                mqtt_broker_ip[0] = '\0';
                return ESP_ERR_NVS_NOT_FOUND;
            }
        } else {
            ESP_LOGW(TAG, "System time not synced; cannot check cache TTL — accepting cached IP for now");
        }
    }

    return ESP_OK;
}

// Save IP + current timestamp (if available). If time not synced, save ts=0.
esp_err_t dns_save_cached_broker_ip(const char* ip) {
    if (ip == NULL || strlen(ip) == 0) return ESP_ERR_INVALID_ARG;
    nvs_handle handle;
    esp_err_t err = nvs_open(DNS_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(handle, DNS_NVS_KEY_BROKER_IP, ip);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    // Save timestamp if time is valid
    uint32_t ts = 0;
    time_t now = time(NULL);
    if (now > 100000) ts = (uint32_t)now; // otherwise leave 0

    err = nvs_set_u32(handle, DNS_NVS_KEY_BROKER_TS, ts);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t dns_clear_cached_broker_ip(void) {
    nvs_handle handle;
    esp_err_t err = nvs_open(DNS_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    nvs_erase_key(handle, DNS_NVS_KEY_BROKER_IP);
    nvs_erase_key(handle, DNS_NVS_KEY_BROKER_TS);
    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

// Quick TCP connect check to see if an IP is reachable on a given port
static bool dns_check_ip_reachable(const char* ip, uint16_t port, uint32_t timeout_ms) {
    if (ip == NULL || strlen(ip) == 0) return false;
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_aton(ip, &addr.sin_addr) == 0) {
        close(sock);
        return false;
    }

    // Non-blocking connect
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    int res = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    if (res == 0) {
        close(sock);
        return true;
    }

    if (errno != EINPROGRESS) {
        close(sock);
        return false;
    }

    fd_set wfds;
    struct timeval tv;
    FD_ZERO(&wfds);
    FD_SET(sock, &wfds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    res = select(sock + 1, NULL, &wfds, NULL, &tv);
    if (res > 0 && FD_ISSET(sock, &wfds)) {
        int so_error = 0;
        socklen_t len = sizeof(so_error);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
        close(sock);
        return (so_error == 0);
    }

    close(sock);
    return false;
}

// Public wrapper
bool dns_is_ip_reachable(const char* ip, uint16_t port, uint32_t timeout_ms)
{
    return dns_check_ip_reachable(ip, port, timeout_ms);
}
