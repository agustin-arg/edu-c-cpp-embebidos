#ifndef DG11J1_PROTOCOL_H_
#define DG11J1_PROTOCOL_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "driver/rmt_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==============================================================================
// 1. CONSTANTES DEL PROTOCOLO DG11J1 / WHIRLPOOL
// ==============================================================================
#define DG11J1_STATE_LENGTH       21    // 21 bytes de payload total
#define DG11J1_TOTAL_BITS         168   // 21 * 8 bits
#define DG11J1_MAX_RMT_SYMBOLS    200   // Cantidad máxima esperada de símbolos RMT

// Rangos de temperatura
#define DG11J1_TEMP_MIN           16    // 16 °C (modelo DG11J1)
#define DG11J1_TEMP_MAX           30    // 30 °C
#define DG11J1_TEMP_DEFAULT       24    // 24 °C inicial

// Tiempos en microsegundos (µs)
#define DG11J1_HDR_MARK_US        8950
#define DG11J1_HDR_SPACE_US       4484
#define DG11J1_BIT_MARK_US        597
#define DG11J1_ONE_SPACE_US       1649
#define DG11J1_ZERO_SPACE_US      533
#define DG11J1_SECTION_GAP_US     7920
#define DG11J1_FOOTER_MARK_US     597

// Modos de operación HVAC
typedef enum {
    AC_MODE_HEAT = 0,
    AC_MODE_AUTO = 1,
    AC_MODE_COOL = 2,
    AC_MODE_DRY  = 3,
    AC_MODE_FAN  = 4
} ac_mode_t;

// Velocidad del ventilador
typedef enum {
    AC_FAN_AUTO = 0,
    AC_FAN_HIGH = 1,
    AC_FAN_MED  = 2,
    AC_FAN_LOW  = 3
} ac_fan_t;

// Códigos de comando de acción (byte 15 del protocolo)
typedef enum {
    AC_CMD_LIGHT     = 0x00,
    AC_CMD_POWER     = 0x01,
    AC_CMD_TEMP      = 0x02,
    AC_CMD_SLEEP     = 0x03,
    AC_CMD_SUPER     = 0x04,
    AC_CMD_ON_TIMER  = 0x05,
    AC_CMD_MODE      = 0x06,
    AC_CMD_SWING     = 0x07,
    AC_CMD_IFEEL     = 0x0D,
    AC_CMD_FAN       = 0x11,
    AC_CMD_6TH_SENSE = 0x17,
    AC_CMD_OFF_TIMER = 0x1D
} ac_cmd_t;

// ==============================================================================
// 2. ESTRUCTURA DE ESTADO DEL AIRE ACONDICIONADO
// ==============================================================================
typedef struct {
    bool      power;     // true = ON, false = OFF
    ac_mode_t mode;      // COOL, HEAT, FAN, DRY, AUTO
    uint8_t   temp_c;    // 16 .. 30 °C
    ac_fan_t  fan;       // AUTO, LOW, MED, HIGH
    bool      swing;     // true = Oscilación activa
    bool      light;     // true = Display encendido
} ac_state_t;

// ==============================================================================
// 3. FUNCIONES PÚBLICAS DE LA API
// ==============================================================================

/**
 * @brief Inicializa un estado por defecto (Power ON, Frío 24°C, Fan Auto, Swing OFF).
 */
void dg11j1_default_state(ac_state_t *state);

/**
 * @brief Construye el buffer crudo de 21 bytes a partir de un estado y comando.
 *
 * @param state Puntero al estado deseado.
 * @param cmd   Comando que gatilló el envío (ej: AC_CMD_TEMP, AC_CMD_POWER, etc.).
 * @param out_bytes Buffer de al menos 21 bytes de salida.
 */
void dg11j1_build_frame(const ac_state_t *state, ac_cmd_t cmd, uint8_t *out_bytes);

/**
 * @brief Convierte un buffer de 21 bytes del protocolo en una secuencia de símbolos RMT.
 *
 * @param frame_bytes Buffer con los 21 bytes generados.
 * @param out_symbols Buffer de símbolos RMT de salida.
 * @param max_symbols Capacidad máxima del buffer out_symbols (debe ser >= 173).
 * @return size_t Cantidad de símbolos RMT generados.
 */
size_t dg11j1_frame_to_rmt_symbols(const uint8_t *frame_bytes, rmt_symbol_word_t *out_symbols, size_t max_symbols);

/**
 * @brief Intenta decodificar un tren de símbolos RMT capturados por el TSOP1738.
 *
 * @param symbols       Buffer de símbolos RMT recibidos.
 * @param num_symbols   Cantidad de símbolos recibidos.
 * @param prev_state    Puntero al estado previo del AC (o NULL). Permite sincronizar Power (Toggle vs Activo).
 * @param out_state     Puntero donde se almacenará el estado decodificado si es exitoso.
 * @param out_cmd       Puntero opcional donde se almacenará el comando ejecutado.
 * @param out_raw_bytes Buffer opcional de al menos 21 bytes donde se copiarán los bytes decodificados (o NULL).
 * @return true si la trama fue decodificada y validada con cabecera y checksums correctos, false si no.
 */
bool dg11j1_decode_rmt_symbols(const rmt_symbol_word_t *symbols, size_t num_symbols,
                               const ac_state_t *prev_state,
                               ac_state_t *out_state, ac_cmd_t *out_cmd,
                               uint8_t *out_raw_bytes);

/**
 * @brief Convierte un array de bytes en una cadena legible hexadecimal separada por espacios.
 */
void dg11j1_bytes_to_hex(const uint8_t *bytes, size_t len, char *out_hex, size_t out_size);

/**
 * @brief Imprime en texto legible el estado del aire acondicionado.
 */
void dg11j1_print_state(const ac_state_t *state);

const char *dg11j1_mode_to_string(ac_mode_t mode);
const char *dg11j1_fan_to_string(ac_fan_t fan);
const char *dg11j1_cmd_to_string(ac_cmd_t cmd);

#ifdef __cplusplus
}
#endif

#endif // DG11J1_PROTOCOL_H_
