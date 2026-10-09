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
 * @brief Inicializa el subsistema Wi-Fi y el gestor de aprovisionamiento por BLE (NimBLE).
 * Si ya está aprovisionado (credenciales guardadas), conecta como Station (STA) y libera BLE.
 * Si no está aprovisionado, inicia el anuncio BLE (PROV_Aire_XXXX) con PoP '12345678'.
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Fuerza el reinicio del aprovisionamiento BLE (borra credenciales previas y reinicia el anuncio).
 * Llamado típicamente por pulsación prolongada (>3s) del botón BOOT.
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

#ifdef __cplusplus
}
#endif

#endif // WIFI_MANAGER_H_
