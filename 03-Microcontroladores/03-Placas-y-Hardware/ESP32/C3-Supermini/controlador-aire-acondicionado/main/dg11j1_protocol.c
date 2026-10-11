#include "dg11j1_protocol.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"

static const char *TAG = "DG11J1_PROTO";

// Tolerancia para coincidencia de tiempos en recepción (±30%)
#define MATCH_TOLERANCE_PCT 30
static inline bool match_duration(uint32_t measured_us, uint32_t expected_us)
{
    uint32_t delta = (expected_us * MATCH_TOLERANCE_PCT) / 100;
    return (measured_us >= (expected_us - delta)) && (measured_us <= (expected_us + delta));
}

void dg11j1_default_state(ac_state_t *state)
{
    if (!state) return;
    state->power = true;
    state->mode = AC_MODE_COOL;
    state->temp_c = DG11J1_TEMP_DEFAULT;
    state->fan = AC_FAN_AUTO;
    state->swing = false;
    state->light = true;
}

const char *dg11j1_mode_to_string(ac_mode_t mode)
{
    switch (mode) {
        case AC_MODE_HEAT: return "CALOR (Heat)";
        case AC_MODE_AUTO: return "AUTOMATICO (Auto)";
        case AC_MODE_COOL: return "FRIO (Cool)";
        case AC_MODE_DRY:  return "DESHUMIDIFICADOR (Dry)";
        case AC_MODE_FAN:  return "VENTILACION (Fan)";
        default:           return "DESCONOCIDO";
    }
}

const char *dg11j1_fan_to_string(ac_fan_t fan)
{
    switch (fan) {
        case AC_FAN_AUTO: return "AUTO";
        case AC_FAN_LOW:  return "BAJA (1)";
        case AC_FAN_MED:  return "MEDIA (2)";
        case AC_FAN_HIGH: return "ALTA (3)";
        default:          return "DESCONOCIDO";
    }
}

const char *dg11j1_cmd_to_string(ac_cmd_t cmd)
{
    switch (cmd) {
        case AC_CMD_LIGHT:     return "Light/Display";
        case AC_CMD_POWER:     return "Power Toggle";
        case AC_CMD_TEMP:      return "Set Temperatura";
        case AC_CMD_SLEEP:     return "Sleep";
        case AC_CMD_SUPER:     return "Super / Jet / Turbo";
        case AC_CMD_ON_TIMER:  return "Timer ON";
        case AC_CMD_MODE:      return "Cambio de Modo";
        case AC_CMD_SWING:     return "Swing";
        case AC_CMD_IFEEL:     return "iFeel";
        case AC_CMD_FAN:       return "Velocidad de Fan";
        case AC_CMD_6TH_SENSE: return "6th Sense";
        case AC_CMD_OFF_TIMER: return "Timer OFF";
        default:               return "General";
    }
}

void dg11j1_print_state(const ac_state_t *state)
{
    if (!state) return;
    ESP_LOGI(TAG, "Estado AC: Power=%s | Modo=%s | Temp=%d C | Fan=%s | Swing=%s | Luz=%s",
             state->power ? "ON" : "OFF",
             dg11j1_mode_to_string(state->mode),
             state->temp_c,
             dg11j1_fan_to_string(state->fan),
             state->swing ? "ON" : "OFF",
             state->light ? "ON" : "OFF");
}

void dg11j1_build_frame(const ac_state_t *state, ac_cmd_t cmd, uint8_t *out_bytes)
{
    if (!state || !out_bytes) return;

    memset(out_bytes, 0, DG11J1_STATE_LENGTH);

    // Cabecera fija de identificación DG11J1 / Whirlpool
    out_bytes[0] = 0x83;
    out_bytes[1] = 0x06;

    // Byte 2: Fan (bits 0-1), Power Toggle (bit 2), Sleep (bit 3), Swing1 (bit 7)
    uint8_t b2 = 0;
    b2 |= (state->fan & 0x03);
    // En el protocolo DG11J1, bit 2 es "Power TOGGLE", no nivel estático de Power.
    // Solo se debe enviar en 1 si la acción fue presionar el botón de encendido/apagado.
    if (cmd == AC_CMD_POWER) {
        b2 |= (1 << 2);
    }
    if (state->swing) {
        b2 |= (1 << 7);
    }
    out_bytes[2] = b2;

    // Byte 3: Mode (bits 0-2), Temp (bits 4-7)
    // Offset de temperatura: En DG11J1-91/Marshall el rango es 16-30 -> temp_val = temp - 16
    uint8_t temp = state->temp_c;
    if (temp < DG11J1_TEMP_MIN) temp = DG11J1_TEMP_MIN;
    if (temp > DG11J1_TEMP_MAX) temp = DG11J1_TEMP_MAX;
    uint8_t temp_val = temp - DG11J1_TEMP_MIN;

    uint8_t b3 = (state->mode & 0x07) | ((temp_val & 0x0F) << 4);
    out_bytes[3] = b3;

    // Byte 4: 0x00
    out_bytes[4] = 0x00;

    // Byte 5: Super / Jet (bit 4, bit 7)
    out_bytes[5] = 0x00;

    // Byte 6: Clock Hours (bits 0-4), LightOff (bit 5), Constante 0x80 (bit 7)
    uint8_t b6 = 0x80; // Constante del protocolo
    if (!state->light) {
        b6 |= (1 << 5); // LightOff = 1 cuando la luz está apagada
    }
    out_bytes[6] = b6;

    // Byte 7: Clock Mins (bits 0-5), OffTimerEnabled (bit 7)
    out_bytes[7] = 0x00;

    // Byte 8: OffHours (bits 0-4), Swing2 (bit 6)
    uint8_t b8 = 0x00;
    if (state->swing) {
        b8 |= (1 << 6);
    }
    out_bytes[8] = b8;

    // Bytes 9-12: Timers / Reserved
    out_bytes[9]  = 0x00;
    out_bytes[10] = 0x00;
    out_bytes[11] = 0x00;
    out_bytes[12] = 0x00;

    // Byte 13: Checksum 1 = XOR de bytes 2 a 12 inclusive
    uint8_t sum1 = 0;
    for (int i = 2; i <= 12; i++) {
        sum1 ^= out_bytes[i];
    }
    out_bytes[13] = sum1;

    // Byte 14: 0x00
    out_bytes[14] = 0x00;

    // Byte 15: Command action
    out_bytes[15] = (uint8_t)cmd;

    // Bytes 16-17: 0x00
    out_bytes[16] = 0x00;
    out_bytes[17] = 0x00;

    // Byte 18: Flag modelo DG11J1 (bit 3 activo para serie DG11J1 / 16-30C)
    out_bytes[18] = (1 << 3);

    // Byte 19: 0x00
    out_bytes[19] = 0x00;

    // Byte 20: Checksum 2 = XOR de bytes 14 a 19 inclusive
    uint8_t sum2 = 0;
    for (int i = 14; i <= 19; i++) {
        sum2 ^= out_bytes[i];
    }
    out_bytes[20] = sum2;
}

size_t dg11j1_frame_to_rmt_symbols(const uint8_t *frame_bytes, rmt_symbol_word_t *out_symbols, size_t max_symbols)
{
    if (!frame_bytes || !out_symbols || max_symbols < DG11J1_MAX_RMT_SYMBOLS) return 0;

    size_t s_idx = 0;

    // -------------------------------------------------------------
    // Sección 1: Cabecera (8950 µs Mark, 4484 µs Space) + 6 Bytes (0..5) + Gap (7920 µs)
    // -------------------------------------------------------------
    out_symbols[s_idx].duration0 = DG11J1_HDR_MARK_US;
    out_symbols[s_idx].level0 = 1;
    out_symbols[s_idx].duration1 = DG11J1_HDR_SPACE_US;
    out_symbols[s_idx].level1 = 0;
    s_idx++;

    // Bytes 0 a 5
    for (int b = 0; b < 6; b++) {
        uint8_t val = frame_bytes[b];
        for (int bit = 0; bit < 8; bit++) {
            bool is_one = (val >> bit) & 1;
            out_symbols[s_idx].duration0 = DG11J1_BIT_MARK_US;
            out_symbols[s_idx].level0 = 1;
            out_symbols[s_idx].duration1 = is_one ? DG11J1_ONE_SPACE_US : DG11J1_ZERO_SPACE_US;
            out_symbols[s_idx].level1 = 0;
            s_idx++;
        }
    }

    // Gap al final de Sección 1: el último símbolo ya tuvo su space, emitimos Mark + Gap space
    out_symbols[s_idx].duration0 = DG11J1_BIT_MARK_US;
    out_symbols[s_idx].level0 = 1;
    out_symbols[s_idx].duration1 = DG11J1_SECTION_GAP_US;
    out_symbols[s_idx].level1 = 0;
    s_idx++;

    // -------------------------------------------------------------
    // Sección 2: 8 Bytes (6..13) + Gap (7920 µs)
    // -------------------------------------------------------------
    for (int b = 6; b < 14; b++) {
        uint8_t val = frame_bytes[b];
        for (int bit = 0; bit < 8; bit++) {
            bool is_one = (val >> bit) & 1;
            out_symbols[s_idx].duration0 = DG11J1_BIT_MARK_US;
            out_symbols[s_idx].level0 = 1;
            out_symbols[s_idx].duration1 = is_one ? DG11J1_ONE_SPACE_US : DG11J1_ZERO_SPACE_US;
            out_symbols[s_idx].level1 = 0;
            s_idx++;
        }
    }

    // Gap al final de Sección 2
    out_symbols[s_idx].duration0 = DG11J1_BIT_MARK_US;
    out_symbols[s_idx].level0 = 1;
    out_symbols[s_idx].duration1 = DG11J1_SECTION_GAP_US;
    out_symbols[s_idx].level1 = 0;
    s_idx++;

    // -------------------------------------------------------------
    // Sección 3: 7 Bytes (14..20) + Footer Mark
    // -------------------------------------------------------------
    for (int b = 14; b < 21; b++) {
        uint8_t val = frame_bytes[b];
        for (int bit = 0; bit < 8; bit++) {
            bool is_one = (val >> bit) & 1;
            out_symbols[s_idx].duration0 = DG11J1_BIT_MARK_US;
            out_symbols[s_idx].level0 = 1;
            out_symbols[s_idx].duration1 = is_one ? DG11J1_ONE_SPACE_US : DG11J1_ZERO_SPACE_US;
            out_symbols[s_idx].level1 = 0;
            s_idx++;
        }
    }

    // Footer final: pulso Mark de cierre
    out_symbols[s_idx].duration0 = DG11J1_FOOTER_MARK_US;
    out_symbols[s_idx].level0 = 1;
    out_symbols[s_idx].duration1 = 0;
    out_symbols[s_idx].level1 = 0;
    s_idx++;

    return s_idx;
}

bool dg11j1_decode_rmt_symbols(const rmt_symbol_word_t *symbols, size_t num_symbols,
                               const ac_state_t *prev_state,
                               ac_state_t *out_state, ac_cmd_t *out_cmd,
                               uint8_t *out_raw_bytes)
{
    if (!symbols || num_symbols < 170) return false;

    // 1. Buscar cabecera DG11J1 (Mark ~8950 µs, Space ~4484 µs)
    // Permite tolerar ruidos, espurias o desfases temporales previos en el TSOP
    size_t header_idx = 0;
    bool header_found = false;

    for (size_t i = 0; i < num_symbols; i++) {
        if ((num_symbols - i) < 170) {
            break;
        }
        if (match_duration(symbols[i].duration0, DG11J1_HDR_MARK_US) &&
            match_duration(symbols[i].duration1, DG11J1_HDR_SPACE_US)) {
            header_idx = i;
            header_found = true;
            break;
        }
    }

    if (!header_found) {
        ESP_LOGD(TAG, "No se halló cabecera válida DG11J1 en %u símbolos", (unsigned int)num_symbols);
        return false;
    }

    size_t s_idx = header_idx + 1;
    uint8_t decoded_bytes[DG11J1_STATE_LENGTH];
    memset(decoded_bytes, 0, sizeof(decoded_bytes));

    // Definición de las 3 secciones: {bytes_start, count, has_gap_after}
    const struct {
        uint8_t byte_start;
        uint8_t count;
        bool has_gap;
    } sections[3] = {
        {0, 6, true},
        {6, 8, true},
        {14, 7, false}
    };

    for (int s = 0; s < 3; s++) {
        for (int b = 0; b < sections[s].count; b++) {
            uint8_t byte_idx = sections[s].byte_start + b;
            uint8_t byte_val = 0;

            for (int bit = 0; bit < 8; bit++) {
                if (s_idx >= num_symbols) return false;

                // En TSOP demodulado, level0 es el pulso activo de portadora (Mark)
                uint32_t mark_us = symbols[s_idx].duration0;
                uint32_t space_us = symbols[s_idx].duration1;

                if (!match_duration(mark_us, DG11J1_BIT_MARK_US)) {
                    return false;
                }

                if (match_duration(space_us, DG11J1_ONE_SPACE_US)) {
                    byte_val |= (1 << bit);
                } else if (match_duration(space_us, DG11J1_ZERO_SPACE_US)) {
                    // Bit es 0
                } else {
                    return false; // Duración de space no concuerda
                }
                s_idx++;
            }
            decoded_bytes[byte_idx] = byte_val;
        }

        // Si la sección tiene Gap intermedio, verificar el símbolo de separación
        if (sections[s].has_gap) {
            if (s_idx >= num_symbols) return false;
            if (!match_duration(symbols[s_idx].duration0, DG11J1_BIT_MARK_US) ||
                !match_duration(symbols[s_idx].duration1, DG11J1_SECTION_GAP_US)) {
                return false;
            }
            s_idx++;
        }
    }

    // 2. Validar firma de identificación del protocolo DG11J1 / Whirlpool (0x83 0x06)
    if (decoded_bytes[0] != 0x83 || decoded_bytes[1] != 0x06) {
        ESP_LOGW(TAG, "Firma DG11J1 inválida (esperado 0x83 0x06, recibido 0x%02X 0x%02X)",
                 decoded_bytes[0], decoded_bytes[1]);
        return false;
    }

    // 3. Validar Checksums XOR
    // Sum1 = XOR de bytes 2 a 12 inclusive
    uint8_t sum1 = 0;
    for (int i = 2; i <= 12; i++) {
        sum1 ^= decoded_bytes[i];
    }
    if (sum1 != decoded_bytes[13]) {
        ESP_LOGW(TAG, "Fallo Checksum 1: calculado=0x%02X, recibido=0x%02X", sum1, decoded_bytes[13]);
        return false;
    }

    // Sum2 = XOR de bytes 14 a 19 inclusive
    uint8_t sum2 = 0;
    for (int i = 14; i <= 19; i++) {
        sum2 ^= decoded_bytes[i];
    }
    if (sum2 != decoded_bytes[20]) {
        ESP_LOGW(TAG, "Fallo Checksum 2: calculado=0x%02X, recibido=0x%02X", sum2, decoded_bytes[20]);
        return false;
    }

    // 4. Extraer variables al estado consolidado
    ac_cmd_t cmd = (ac_cmd_t)decoded_bytes[15];

    if (out_state) {
        if (prev_state != NULL) {
            *out_state = *prev_state;
        } else {
            dg11j1_default_state(out_state);
        }

        // Lógica de encendido/apagado en DG11J1:
        // Bit 2 de byte 2 es 'Power Toggle'.
        // Si el usuario pulsó el botón Power (cmd == AC_CMD_POWER o bit 2 activo), conmuta el estado de encendido.
        // Si pulsó cualquier otro ajuste (Temp, Modo, Fan, Swing), el equipo está operando activamente (ON).
        if (cmd == AC_CMD_POWER || (decoded_bytes[2] & (1 << 2)) != 0) {
            out_state->power = prev_state ? !prev_state->power : true;
        } else {
            out_state->power = true;
        }

        out_state->fan   = (ac_fan_t)(decoded_bytes[2] & 0x03);
        out_state->swing = (decoded_bytes[2] & (1 << 7)) != 0;

        out_state->mode  = (ac_mode_t)(decoded_bytes[3] & 0x07);
        uint8_t temp_val = (decoded_bytes[3] >> 4) & 0x0F;
        out_state->temp_c = temp_val + DG11J1_TEMP_MIN;
        if (out_state->temp_c < DG11J1_TEMP_MIN) out_state->temp_c = DG11J1_TEMP_MIN;
        if (out_state->temp_c > DG11J1_TEMP_MAX) out_state->temp_c = DG11J1_TEMP_MAX;

        out_state->light = ((decoded_bytes[6] & (1 << 5)) == 0);
    }

    if (out_cmd) {
        *out_cmd = cmd;
    }

    if (out_raw_bytes) {
        memcpy(out_raw_bytes, decoded_bytes, DG11J1_STATE_LENGTH);
    }

    ESP_LOGI(TAG, "Trama DG11J1 decodificada y validada con éxito! (Cmd: %s, Power: %s, Temp: %d C)",
             dg11j1_cmd_to_string(cmd),
             (out_state && out_state->power) ? "ON" : "OFF",
             out_state ? out_state->temp_c : 0);
    return true;
}

void dg11j1_bytes_to_hex(const uint8_t *bytes, size_t len, char *out_hex, size_t out_size)
{
    if (!out_hex || out_size == 0) return;
    out_hex[0] = '\0';
    if (!bytes || len == 0) return;

    size_t pos = 0;
    for (size_t i = 0; i < len && pos + 3 < out_size; i++) {
        int written = snprintf(out_hex + pos, out_size - pos, "%s%02X", (i > 0) ? " " : "", bytes[i]);
        if (written > 0) {
            pos += written;
        }
    }
}
