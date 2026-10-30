#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

static const char *TAG = "WIFI_DIAG";

#define WIFI_SSID      "Personal-CE7-2.4GHz"
#define WIFI_PASS      "26enero460"
#define MAX_RETRY      5

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;
static bool s_is_connected = false;

static const char* get_disconnect_reason_str(uint8_t reason)
{
    switch (reason) {
        case WIFI_REASON_UNSPECIFIED: return "UNSPECIFIED";
        case WIFI_REASON_AUTH_EXPIRE: return "AUTH_EXPIRE";
        case WIFI_REASON_NOT_AUTHED: return "NOT_AUTHED";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4WAY_HANDSHAKE_TIMEOUT (clave incorrecta o senal debil)";
        case WIFI_REASON_BEACON_TIMEOUT: return "BEACON_TIMEOUT (perdida de balizas / senal muy debil)";
        case WIFI_REASON_NO_AP_FOUND: return "NO_AP_FOUND (no se encontro la red con ese SSID)";
        case WIFI_REASON_AUTH_FAIL: return "AUTH_FAIL (fallo de autenticacion)";
        case WIFI_REASON_ASSOC_FAIL: return "ASSOC_FAIL (fallo de asociacion con el router)";
        case WIFI_REASON_HANDSHAKE_TIMEOUT: return "HANDSHAKE_TIMEOUT";
        case WIFI_REASON_CONNECTION_FAIL: return "CONNECTION_FAIL";
        default: return "CODIGO DE ERROR DESCONOCIDO";
    }
}

static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Iniciando conexion a '%s'...", WIFI_SSID);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* disconn = (wifi_event_sta_disconnected_t*) event_data;
        s_is_connected = false;
        ESP_LOGW(TAG, "Desconectado del AP. Razon [%d]: %s", disconn->reason, get_disconnect_reason_str(disconn->reason));

        if (s_retry_num < MAX_RETRY) {
            s_retry_num++;
            ESP_LOGI(TAG, "Reintentando conectar al AP (%d/%d)...", s_retry_num, MAX_RETRY);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "==================================================");
        ESP_LOGI(TAG, ">>> ¡CONECTADO CON EXITO AL WIFI! <<<");
        ESP_LOGI(TAG, "IP Asignada: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Mascara:     " IPSTR, IP2STR(&event->ip_info.netmask));
        ESP_LOGI(TAG, "Gateway:     " IPSTR, IP2STR(&event->ip_info.gw));
        ESP_LOGI(TAG, "==================================================");
        s_retry_num = 0;
        s_is_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void scan_nearby_networks(void)
{
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, ">> FASE 1: ESCANEO DE REDES WI-FI 2.4 GHz <<");
    ESP_LOGI(TAG, "   (Comprobando antena y receptor de radio RF)");
    ESP_LOGI(TAG, "--------------------------------------------------");

    wifi_scan_config_t scan_config = {
        .ssid = 0,
        .bssid = 0,
        .channel = 0,
        .show_hidden = true
    };
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));

    uint16_t ap_count = 0;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    ESP_LOGI(TAG, "Total de redes detectadas en el aire: %d", ap_count);

    if (ap_count > 0) {
        wifi_ap_record_t *ap_records = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_count);
        if (ap_records) {
            ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_records));
            printf("\n%-4s | %-28s | %-6s | %-4s | %s\n", "Num", "SSID", "RSSI", "Canal", "Objetivo");
            printf("----------------------------------------------------------------------\n");
            for (int i = 0; i < ap_count; i++) {
                bool is_target = (strcmp((char *)ap_records[i].ssid, WIFI_SSID) == 0);
                printf("[%2d] | %-28s | %3d dBm | Ch %2d  | %s\n",
                       i + 1,
                       (char *)ap_records[i].ssid,
                       ap_records[i].rssi,
                       ap_records[i].primary,
                       is_target ? " <=== TU RED" : "");
            }
            printf("----------------------------------------------------------------------\n\n");
            free(ap_records);
        }
    } else {
        ESP_LOGE(TAG, "¡ALERTA! El escaner no detecto ninguna red. Podria indicar antena danada o desconectada.");
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // 1. Escaneo preliminar para verificar sensibilidad de antena RF
    scan_nearby_networks();

    // 2. Configuración y conexión a la red
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, ">> FASE 2: CONECTANDO A LA RED '%s' <<", WIFI_SSID);
    ESP_LOGI(TAG, "--------------------------------------------------");

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    // Desactivar ahorro de energia del modem (clave para la estabilidad en el Supermini)
    esp_wifi_set_ps(WIFI_PS_NONE);

    esp_wifi_connect();

    // 3. Monitoreo continuo de calidad de enlace
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(3000));

        if (s_is_connected) {
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                const char* calificacion;
                if (ap_info.rssi >= -60) calificacion = "EXCELENTE (Potente)";
                else if (ap_info.rssi >= -75) calificacion = "BUENA / MEDIA";
                else if (ap_info.rssi >= -85) calificacion = "DEBIL (Riesgo de microcortes)";
                else calificacion = "MUY CRITICA (Al borde de desconexion)";

                ESP_LOGI(TAG, "[MONITOREO] Enlace Estable | RSSI: %d dBm -> %s", ap_info.rssi, calificacion);
            }
        }
    }
}
