#ifndef TIME_MANAGER_H_
#define TIME_MANAGER_H_

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicializa el cliente SNTP y configura la zona horaria ART3.
 *        Si ya fue inicializado, reinicia la consulta.
 */
esp_err_t time_manager_init(void);

/**
 * @brief Retorna true si el reloj interno fue sincronizado exitosamente con NTP.
 */
bool time_manager_is_synced(void);

/**
 * @brief Obtiene la hora actual formateada en string legible (ej: "13:05:22").
 */
esp_err_t time_manager_get_time_str(char *buf, size_t max_len);

/**
 * @brief Obtiene la fecha y hora completa en formato ISO 8601 (ej: "2026-10-09T13:05:22-03:00").
 */
esp_err_t time_manager_get_iso8601(char *buf, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif // TIME_MANAGER_H_
