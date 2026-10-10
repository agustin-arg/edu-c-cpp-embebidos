#include "wifi_manager.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_coexist.h"

#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_ble.h>
#include "time_manager.h"
#include "mqtt_manager.h"

static const char *TAG = "WIFI_MGR";

#define PROV_DEVICE_PREFIX   "PROV_Aire_"
#define PROV_POP_CODE         "12345678"
#define WIFI_CONNECTED_BIT    BIT0

// 48 = 12 dBm (48 * 0.25 dBm): previene picos de consumo >350mA en regulador y antena del C3 SuperMini
#define C3_SUPERMINI_TX_POWER 48

static EventGroupHandle_t s_wifi_event_group = NULL;
static wifi_mgr_state_t   s_current_state = WIFI_MGR_STA_DISCONNECTED;
static char               s_service_name[32] = {0};
static char               s_ip_str[16] = "0.0.0.0";
static bool               s_is_provisioned = false;

// UUID de 128-bit estándar de Espressif para la app oficial 'ESP BLE Provisioning'
static const uint8_t s_custom_service_uuid[] = {
    0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
    0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02,
};

static void get_device_service_name(char *service_name, size_t max)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(service_name, max, "%s%02X%02X", PROV_DEVICE_PREFIX, mac[4], mac[5]);
}

static void wifi_prov_event_handler(void *arg, esp_event_base_t event_base,
                                   int32_t event_id, void *event_data)
{
    if (event_base == WIFI_PROV_EVENT) {
        switch (event_id) {
            case WIFI_PROV_START:
                ESP_LOGI(TAG, "Aprovisionamiento BLE iniciado.");
                s_current_state = WIFI_MGR_PROV_BLE_ACTIVE;
                break;
            case WIFI_PROV_CRED_RECV: {
                wifi_sta_config_t *sta_cfg = (wifi_sta_config_t *)event_data;
                ESP_LOGI(TAG, "Credenciales Wi-Fi recibidas desde app celular: SSID '%s'", (const char *)sta_cfg->ssid);
                break;
            }
            case WIFI_PROV_CRED_FAIL: {
                wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
                ESP_LOGE(TAG, "Fallo en aprovisionamiento: %s",
                         (*reason == WIFI_PROV_STA_AUTH_ERROR) ? "Clave incorrecta" : "Red Wi-Fi no encontrada");
                break;
            }
            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI(TAG, "¡Conexión Wi-Fi exitosa con las credenciales provistas!");
                s_is_provisioned = true;
                break;
            case WIFI_PROV_END:
                ESP_LOGI(TAG, "Aprovisionamiento finalizado. Liberando memoria BLE.");
                wifi_prov_mgr_deinit();
                break;
            default:
                break;
        }
    } else if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                esp_wifi_set_ps(WIFI_PS_NONE);
                esp_wifi_set_max_tx_power(C3_SUPERMINI_TX_POWER);
                s_current_state = WIFI_MGR_STA_CONNECTING;
                if (s_is_provisioned) {
                    esp_wifi_connect();
                }
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                s_current_state = WIFI_MGR_STA_DISCONNECTED;
                mqtt_manager_stop();
                if (s_is_provisioned) {
                    ESP_LOGW(TAG, "Wi-Fi desconectado. Intentando reconectar...");
                    esp_wifi_connect();
                }
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, ">>> ¡CONECTADO A WI-FI! IP Asignada: %s <<<", s_ip_str);
        s_current_state = WIFI_MGR_STA_CONNECTED;
        time_manager_init();
        mqtt_manager_start();
        if (s_wifi_event_group) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
    } else if (event_base == PROTOCOMM_TRANSPORT_BLE_EVENT) {
        switch (event_id) {
            case PROTOCOMM_TRANSPORT_BLE_CONNECTED:
                ESP_LOGI(TAG, "Dispositivo celular conectado por BLE.");
                break;
            case PROTOCOMM_TRANSPORT_BLE_DISCONNECTED:
                ESP_LOGI(TAG, "Dispositivo celular desconectado de BLE.");
                break;
            default:
                break;
        }
    }
}

void wifi_manager_start_ble_provisioning(void)
{
    ESP_LOGW(TAG, "Reinicio de aprovisionamiento solicitado por usuario.");
    wifi_prov_mgr_reset_provisioning();
    esp_restart();
}

esp_err_t wifi_manager_init(void)
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

    // Configuración regulatoria para Argentina (canales 1 a 13)
    wifi_country_t country = {
        .cc = "AR",
        .schan = 1,
        .nchan = 13,
        .max_tx_power = C3_SUPERMINI_TX_POWER,
        .policy = WIFI_COUNTRY_POLICY_AUTO
    };
    esp_wifi_set_country(&country);

    // Priorizar tráfico Wi-Fi frente a BLE en el arbitraje de coexistencia de radio
    esp_coex_preference_set(ESP_COEX_PREFER_WIFI);

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &wifi_prov_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(PROTOCOMM_TRANSPORT_BLE_EVENT, ESP_EVENT_ANY_ID, &wifi_prov_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_prov_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_prov_event_handler, NULL));

    // Inicializar el gestor de aprovisionamiento con liberación automática de memoria Bluetooth
    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM
    };
    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&s_is_provisioned));

    if (s_is_provisioned) {
        ESP_LOGI(TAG, "Dispositivo ya aprovisionado. Liberando stack BLE y arrancando Wi-Fi STA...");
        wifi_prov_mgr_deinit();

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
        esp_wifi_set_ps(WIFI_PS_NONE);
        esp_wifi_set_max_tx_power(C3_SUPERMINI_TX_POWER);
    } else {
        ESP_LOGW(TAG, "Dispositivo no aprovisionado. Levantando servicio BLE Provisioning...");

        get_device_service_name(s_service_name, sizeof(s_service_name));
        wifi_prov_scheme_ble_set_service_uuid((uint8_t *)s_custom_service_uuid);

        wifi_prov_security_t security = WIFI_PROV_SECURITY_1;
        const char *pop = PROV_POP_CODE;
        wifi_prov_security1_params_t *sec_params = (wifi_prov_security1_params_t *)pop;

        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(security, (const void *)sec_params, s_service_name, NULL));

        printf("\n===================================================================\n");
        printf("  [APROVISIONAMIENTO BLE ACTIVO]\n");
        printf("  1. Abre en tu celular la app oficial 'ESP BLE Provisioning'\n");
        printf("     (disponible en Google Play Store / Apple App Store).\n");
        printf("  2. Busca dispositivos BLE con el prefijo: %s\n", s_service_name);
        printf("  3. Ingresa la clave PoP (Proof of Possession): %s\n", PROV_POP_CODE);
        printf("  4. Selecciona tu red Wi-Fi y coloca la contraseña.\n");
        printf("  (Una vez conectado, BLE se apagará automáticamente liberando memoria)\n");
        printf("===================================================================\n\n");
    }

    return ESP_OK;
}

wifi_mgr_state_t wifi_manager_get_state(void)
{
    return s_current_state;
}

bool wifi_manager_is_connected(void)
{
    return (s_current_state == WIFI_MGR_STA_CONNECTED);
}

const char *wifi_manager_get_ip_str(void)
{
    return s_ip_str;
}
