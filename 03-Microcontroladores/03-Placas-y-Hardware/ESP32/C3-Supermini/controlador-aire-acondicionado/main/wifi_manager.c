#include "wifi_manager.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_netif.h"
#include "esp_http_server.h"

static const char *TAG = "WIFI_MGR";

#define NVS_NAMESPACE       "wifi_cfg"
#define NVS_KEY_SSID        "ssid"
#define NVS_KEY_PASS        "pass"

#define SOFTAP_SSID         "Aire-Marshall-Setup"
#define SOFTAP_PASS         ""                  // Red abierta para configuración rápida
#define SOFTAP_MAX_CONN     4
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define MAX_RETRY_COUNT     5

static EventGroupHandle_t s_wifi_event_group = NULL;
static int s_retry_num = 0;
static wifi_mgr_state_t s_current_state = WIFI_MGR_STA_DISCONNECTED;

static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;
static httpd_handle_t s_http_server = NULL;

static char s_saved_ssid[33] = {0};
static char s_saved_pass[65] = {0};

// ==============================================================================
// 1. MANEJO DE NVS (ALMACENAMIENTO PERMANENTE)
// ==============================================================================
static esp_err_t load_credentials_from_nvs(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    size_t required_ssid = ssid_len;
    err = nvs_get_str(handle, NVS_KEY_SSID, ssid, &required_ssid);
    if (err == ESP_OK) {
        size_t required_pass = pass_len;
        err = nvs_get_str(handle, NVS_KEY_PASS, pass, &required_pass);
    }

    nvs_close(handle);
    return err;
}

static esp_err_t save_credentials_to_nvs(const char *ssid, const char *pass)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(handle, NVS_KEY_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, NVS_KEY_PASS, pass);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

// ==============================================================================
// 2. PORTAL WEB DE CONFIGURACIÓN (SOFTAP)
// ==============================================================================
static const char HTML_CONFIG_PAGE[] =
"<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Aire Marshall - Configuración Wi-Fi</title>"
"<style>"
"body{font-family:sans-serif;margin:0;padding:20px;background:#f0f2f5;display:flex;justify-content:center;align-items:center;min-height:90vh;}"
".card{background:#fff;padding:25px;border-radius:12px;box-shadow:0 4px 15px rgba(0,0,0,0.1);max-width:380px;width:100%;}"
"h2{margin-top:0;color:#1e3c72;text-align:center;}"
"p{color:#666;font-size:14px;line-height:1.4;text-align:center;}"
"label{font-weight:bold;font-size:13px;display:block;margin-top:15px;color:#333;}"
"input[type=text],input[type=password]{width:100%;box-sizing:border-box;padding:12px;margin-top:6px;border:1px solid #ccc;border-radius:8px;font-size:15px;}"
"input[type=submit]{width:100%;background:#1e3c72;color:#fff;border:none;padding:14px;border-radius:8px;font-size:16px;font-weight:bold;margin-top:22px;cursor:pointer;}"
"input[type=submit]:hover{background:#2a5298;}"
".footer{margin-top:20px;text-align:center;font-size:12px;color:#999;}"
"</style></head><body>"
"<div class='card'>"
"<h2>❄️ Aire Marshall</h2>"
"<p>Conecta el controlador a tu red Wi-Fi hogareña para habilitar el control remoto.</p>"
"<form action='/save' method='POST'>"
"<label for='ssid'>Nombre de Red Wi-Fi (SSID):</label>"
"<input type='text' id='ssid' name='ssid' required placeholder='Ej: Mi_Fibertel_2.4G'>"
"<label for='pass'>Contraseña:</label>"
"<input type='password' id='pass' name='pass' placeholder='Contraseña de tu red'>"
"<input type='submit' value='Guardar y Conectar'>"
"</form>"
"<div class='footer'>ESP32-C3 SuperMini • Controlador Marshall</div>"
"</div></body></html>";

static const char HTML_SUCCESS_PAGE[] =
"<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Guardado Exitoso</title>"
"<style>"
"body{font-family:sans-serif;margin:0;padding:20px;background:#e8f5e9;display:flex;justify-content:center;align-items:center;min-height:90vh;}"
".card{background:#fff;padding:25px;border-radius:12px;box-shadow:0 4px 15px rgba(0,0,0,0.1);max-width:380px;width:100%;text-align:center;}"
"h2{color:#2e7d32;margin-top:0;}"
"p{color:#555;font-size:14px;line-height:1.5;}"
"</style></head><body>"
"<div class='card'>"
"<h2>✅ ¡Credenciales Guardadas!</h2>"
"<p>El controlador se reiniciará e intentará conectarse a tu red Wi-Fi.</p>"
"<p>Ya puedes volver a conectarte a tu red habitual.</p>"
"</div></body></html>";

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, HTML_CONFIG_PAGE, HTTPD_RESP_USE_STRLEN);
}

static void delayed_restart_task(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

static esp_err_t save_post_handler(httpd_req_t *req)
{
    char buf[200] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        return ESP_FAIL;
    }

    char new_ssid[33] = {0};
    char new_pass[65] = {0};

    // Parseo de formulario application/x-www-form-urlencoded
    // Formato esperado: ssid=...&pass=...
    char *p_ssid = strstr(buf, "ssid=");
    char *p_pass = strstr(buf, "pass=");

    if (p_ssid) {
        p_ssid += 5;
        char *amp = strchr(p_ssid, '&');
        if (amp) *amp = '\0';
        // Decodificación básica de '+' por espacios
        for (char *c = p_ssid; *c; c++) if (*c == '+') *c = ' ';
        strncpy(new_ssid, p_ssid, sizeof(new_ssid) - 1);
    }

    if (p_pass) {
        p_pass += 5;
        char *amp = strchr(p_pass, '&');
        if (amp) *amp = '\0';
        for (char *c = p_pass; *c; c++) if (*c == '+') *c = ' ';
        strncpy(new_pass, p_pass, sizeof(new_pass) - 1);
    }

    ESP_LOGI(TAG, "Nuevas credenciales recibidas: SSID='%s'", new_ssid);

    if (strlen(new_ssid) > 0) {
        save_credentials_to_nvs(new_ssid, new_pass);
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, HTML_SUCCESS_PAGE, HTTPD_RESP_USE_STRLEN);

        xTaskCreate(delayed_restart_task, "restart_task", 2048, NULL, 5, NULL);
        return ESP_OK;
    }

    httpd_resp_send_500(req);
    return ESP_FAIL;
}

static void start_web_config_server(void)
{
    if (s_http_server != NULL) return;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;

    if (httpd_start(&s_http_server, &config) == ESP_OK) {
        httpd_uri_t root_uri = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = root_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(s_http_server, &root_uri);

        httpd_uri_t save_uri = {
            .uri       = "/save",
            .method    = HTTP_POST,
            .handler   = save_post_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(s_http_server, &save_uri);

        ESP_LOGI(TAG, "Servidor web de configuración activo en puerto 80.");
    }
}

// ==============================================================================
// 3. EVENT HANDLER Y CONTROL DE ESTADOS WI-FI
// ==============================================================================
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        s_current_state = WIFI_MGR_STA_CONNECTING;
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_current_state != WIFI_MGR_SOFTAP_ACTIVE) {
            if (s_retry_num < MAX_RETRY_COUNT) {
                s_retry_num++;
                ESP_LOGI(TAG, "Reintentando conexión Wi-Fi (%d/%d)...", s_retry_num, MAX_RETRY_COUNT);
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_wifi_connect();
            } else {
                ESP_LOGW(TAG, "Fallo al conectar a '%s'. Iniciando SoftAP de rescate...", s_saved_ssid);
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                wifi_manager_start_softap();
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, ">>> CONECTADO A WI-FI! IP: " IPSTR " <<<", IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        s_current_state = WIFI_MGR_STA_CONNECTED;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_manager_start_softap(void)
{
    ESP_LOGI(TAG, "Activando Modo SoftAP: '%s'...", SOFTAP_SSID);

    s_current_state = WIFI_MGR_SOFTAP_ACTIVE;

    // Detener modo Station si estaba activo
    esp_wifi_stop();

    if (s_ap_netif == NULL) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
    }

    wifi_config_t ap_config = {
        .ap = {
            .ssid = SOFTAP_SSID,
            .ssid_len = strlen(SOFTAP_SSID),
            .channel = 1,
            .password = SOFTAP_PASS,
            .max_connection = SOFTAP_MAX_CONN,
            .authmode = WIFI_AUTH_OPEN
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    printf("\n===================================================================\n");
    printf("  [MODO SOFTAP ACTIVO] Red creada: %s\n", SOFTAP_SSID);
    printf("  1. Conéctate con tu celular a la red Wi-Fi: %s\n", SOFTAP_SSID);
    printf("  2. Abre en tu navegador la dirección: http://192.168.4.1\n");
    printf("  3. Ingresa tu red Wi-Fi y contraseña para conectar el aire.\n");
    printf("===================================================================\n\n");

    start_web_config_server();
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

    s_sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    // Intentar leer credenciales guardadas en NVS
    esp_err_t nvs_err = load_credentials_from_nvs(s_saved_ssid, sizeof(s_saved_ssid),
                                                 s_saved_pass, sizeof(s_saved_pass));

    if (nvs_err == ESP_OK && strlen(s_saved_ssid) > 0) {
        ESP_LOGI(TAG, "Credenciales encontradas en NVS para red '%s'. Intentando conexión...", s_saved_ssid);
        wifi_config_t wifi_config = {0};
        strncpy((char *)wifi_config.sta.ssid, s_saved_ssid, sizeof(wifi_config.sta.ssid));
        strncpy((char *)wifi_config.sta.password, s_saved_pass, sizeof(wifi_config.sta.password));
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());
    } else {
        ESP_LOGW(TAG, "No hay credenciales Wi-Fi en NVS. Iniciando portal SoftAP por defecto...");
        wifi_manager_start_softap();
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
