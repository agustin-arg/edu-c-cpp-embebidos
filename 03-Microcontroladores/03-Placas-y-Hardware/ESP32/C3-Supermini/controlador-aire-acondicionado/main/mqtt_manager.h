#ifndef MQTT_MANAGER_H_
#define MQTT_MANAGER_H_

#include <stdbool.h>
#include "esp_err.h"
#include "dg11j1_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

// Dirección del broker Mosquitto en la Raspberry Pi 4 B
#define MQTT_BROKER_URI       "mqtt://192.168.0.188:1883"

// Jerarquía de tópicos MQTT
#define MQTT_TOPIC_BASE       "aire/living"
#define MQTT_TOPIC_AVAIL      MQTT_TOPIC_BASE "/availability"
#define MQTT_TOPIC_STATUS     MQTT_TOPIC_BASE "/status"
#define MQTT_TOPIC_SET_SUB    MQTT_TOPIC_BASE "/set/#"

// Callback invocado cuando se recibe una directiva remota válida por MQTT
typedef void (*mqtt_command_handler_t)(const ac_state_t *target_state, ac_cmd_t cmd);

// Callback invocado por el gestor MQTT para obtener el estado actual del equipo
typedef void (*mqtt_state_getter_t)(ac_state_t *out_state);

/**
 * @brief Inicializa el subsistema MQTT con sus handlers de eventos y callbacks.
 *
 * @param cmd_handler Callback que recibe el nuevo estado consolidado y el comando a emitir por IR.
 * @param state_getter Callback que retorna una copia del estado actual del AC.
 * @return ESP_OK en caso de éxito.
 */
esp_err_t mqtt_manager_init(mqtt_command_handler_t cmd_handler, mqtt_state_getter_t state_getter);

/**
 * @brief Inicia la conexión con el broker MQTT.
 */
esp_err_t mqtt_manager_start(void);

/**
 * @brief Detiene el cliente MQTT.
 */
esp_err_t mqtt_manager_stop(void);

/**
 * @brief Retorna true si el cliente está conectado activamente al broker MQTT.
 */
bool mqtt_manager_is_connected(void);

/**
 * @brief Publica el estado actual en formato JSON en 'aire/living/status' (retain=true).
 *
 * @param state Puntero al estado actual a reportar.
 * @return ESP_OK si el mensaje se encoló con éxito para publicación.
 */
esp_err_t mqtt_manager_publish_state(const ac_state_t *state);

/**
 * @brief Publica el payload de Home Assistant MQTT Discovery en 'homeassistant/climate/.../config' (retain=true).
 *
 * @return ESP_OK si se encoló con éxito.
 */
esp_err_t mqtt_manager_publish_discovery(void);

#ifdef __cplusplus
}
#endif

#endif // MQTT_MANAGER_H_
