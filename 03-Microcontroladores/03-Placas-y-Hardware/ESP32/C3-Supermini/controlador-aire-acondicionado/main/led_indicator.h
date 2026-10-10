#ifndef LED_INDICATOR_H
#define LED_INDICATOR_H

#include <stdbool.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Configuración de hardware del LED onboard:
 * En el ESP32-C3 SuperMini, el LED integrado (azul) está en GPIO 8 y conectado
 * con lógica activa en nivel BAJO (ánodo a 3.3V, cátodo a GPIO 8).
 * Si se desea usar un LED externo en otro pin (ej. GPIO 2), modificar aquí.
 */
#define STATUS_LED_GPIO         GPIO_NUM_8
#define STATUS_LED_ACTIVE_LOW   1   // 1: Activo en bajo (ESP32-C3 SuperMini onboard), 0: Activo en alto

/**
 * Modos continuos de funcionamiento del indicador LED
 */
typedef enum {
    LED_MODE_OFF = 0,         ///< Reposo nocturno / silencio visual (LED apagado)
    LED_MODE_PROVISIONING,    ///< Modo respiración suave (aprovisionamiento BLE activo)
} led_mode_t;

/**
 * Eventos visuales transitorios (animaciones con retorno automático al modo base)
 */
typedef enum {
    LED_EVENT_IR_TX = 0,      ///< Ráfaga rápida difuminada (3 destellos) al transmitir IR al split
    LED_EVENT_IR_RX,          ///< Doble pulso suave difuminado al decodificar control físico
    LED_EVENT_WIFI_CONNECTED, ///< Pulso suave de confirmación al conectar a red Wi-Fi
} led_event_t;

/**
 * @brief Inicializa el periférico LEDC (PWM a 5 kHz, 10 bits de resolución),
 *        instala el driver de desvanecimiento hardware y arranca la tarea de feedback.
 */
void led_indicator_init(void);

/**
 * @brief Establece el modo base continuo del LED (ej. apagado en reposo o respiración en BLE).
 * @param mode Modo de operación deseado.
 */
void led_indicator_set_mode(led_mode_t mode);

/**
 * @brief Dispara un patrón visual transitorio (no bloqueante, encolado para ejecución en segundo plano).
 * @param evt Evento a señalizar.
 */
void led_indicator_trigger(led_event_t evt);

#ifdef __cplusplus
}
#endif

#endif // LED_INDICATOR_H
