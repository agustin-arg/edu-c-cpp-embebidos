#ifndef WIFI_MANAGER_H_
#define WIFI_MANAGER_H_

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_MGR_STA_DISCONNECTED = 0,
    WIFI_MGR_STA_CONNECTING,
    WIFI_MGR_STA_CONNECTED,
    WIFI_MGR_PROV_BLE_ACTIVE
} wifi_mgr_state_t;

/**
 * @brief Inicializa el subsistema Wi-Fi y el gestor de aprovisionamiento BLE (NimBLE).
 * Si ya está aprovisionado (credenciales en NVS), inicia STA y libera el stack BLE.
 * Si no está aprovisionado, inicia el servicio BLE Provisioning (PROV_Aire_XXXX, PoP: 12345678).
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Reinicia el estado de aprovisionamiento en NVS y reinicia el ESP32 para levantar BLE.
 */
void wifi_manager_start_ble_provisioning(void);

/**
 * @brief Obtiene el estado actual de la conexión Wi-Fi / Aprovisionamiento.
 */
wifi_mgr_state_t wifi_manager_get_state(void);

/**
 * @brief Retorna true si el ESP32 está conectado a la red local y con IP asignada.
 */
bool wifi_manager_is_connected(void);

/**
 * @brief Obtiene la dirección IP actual en formato string ("192.168.x.x").
 */
const char *wifi_manager_get_ip_str(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_MANAGER_H_
