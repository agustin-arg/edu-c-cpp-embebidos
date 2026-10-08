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
    WIFI_MGR_SOFTAP_ACTIVE
} wifi_mgr_state_t;

/**
 * @brief Inicializa el subsistema Wi-Fi, NVS y el servidor web de configuración.
 * Intenta conectar a las credenciales guardadas en NVS. Si no existen o fallan, entra en SoftAP.
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Fuerza la entrada al modo SoftAP (Portal Web de configuración).
 */
void wifi_manager_start_softap(void);

/**
 * @brief Obtiene el estado actual de la conexión Wi-Fi.
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
