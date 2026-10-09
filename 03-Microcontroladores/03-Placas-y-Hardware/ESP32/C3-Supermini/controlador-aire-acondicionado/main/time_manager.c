#include "time_manager.h"
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include "esp_sntp.h"
#include "esp_netif_sntp.h"
#include "esp_log.h"

static const char *TAG = "TIME_MGR";
static bool s_time_synced = false;
static bool s_sntp_initialized = false;

static void time_sync_notification_cb(struct timeval *tv)
{
    s_time_synced = true;
    time_t now = tv->tv_sec;
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    ESP_LOGI(TAG, ">>> ¡RELOJ SINCRONIZADO CON NTP! Hora local: %s (ART3 / UTC-3) <<<", strftime_buf);
}

esp_err_t time_manager_init(void)
{
    // Configurar zona horaria de Argentina (ART3 = UTC-3 sin horario de verano)
    setenv("TZ", "ART3", 1);
    tzset();

    if (s_sntp_initialized) {
        ESP_LOGI(TAG, "Servicio SNTP ya inicializado. Reiniciando sincronización...");
        return esp_netif_sntp_start();
    }

    ESP_LOGI(TAG, "Iniciando servicio SNTP con servidores pool.ntp.org (Zona Horaria: ART3)...");

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    config.sync_cb = time_sync_notification_cb;
    config.wait_for_sync = false; // Asíncrono / no bloqueante

    esp_err_t ret = esp_netif_sntp_init(&config);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Fallo al inicializar esp_netif_sntp: %s", esp_err_to_name(ret));
        return ret;
    }

    s_sntp_initialized = true;
    return ESP_OK;
}

bool time_manager_is_synced(void)
{
    return s_time_synced;
}

esp_err_t time_manager_get_time_str(char *buf, size_t max_len)
{
    if (!buf || max_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    strftime(buf, max_len, "%H:%M:%S", &timeinfo);
    return ESP_OK;
}

esp_err_t time_manager_get_iso8601(char *buf, size_t max_len)
{
    if (!buf || max_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    strftime(buf, max_len, "%Y-%m-%dT%H:%M:%S-03:00", &timeinfo);
    return ESP_OK;
}
