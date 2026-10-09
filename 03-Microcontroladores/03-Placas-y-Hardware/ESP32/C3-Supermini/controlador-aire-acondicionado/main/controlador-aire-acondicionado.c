#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_encoder.h"

#include "dg11j1_protocol.h"
#include "wifi_manager.h"

static const char *TAG = "AC_CONTROLLER";

// ==============================================================================
// 1. DEFINICIÓN DE PINES Y PARÁMETROS DE HARDWARE
// ==============================================================================
#define IR_TX_GPIO          GPIO_NUM_0      // LED Emisor Infrarrojo (con transistor NPN)
#define IR_RX_GPIO          GPIO_NUM_6      // Demodulador TSOP1738 (38 kHz, GPIO 6)
#define BOOT_BUTTON_GPIO    GPIO_NUM_9      // Botón BOOT integrado en ESP32-C3 SuperMini

#define IR_RESOLUTION_HZ    1000000         // 1 MHz -> 1 tick = 1 microsegundo (us)
#define IR_CARRIER_FREQ_HZ  38000           // Portadora infrarroja estándar de 38 kHz
#define MAX_IR_SYMBOLS      600             // Capacidad para ráfagas HVAC completas

// Comandos de teclado / interacción
typedef enum {
    CMD_SEND_CURRENT = 0,
    CMD_TEMP_UP,
    CMD_TEMP_DOWN,
    CMD_MODE_NEXT,
    CMD_FAN_NEXT,
    CMD_POWER_TOGGLE,
    CMD_SWING_TOGGLE,
    CMD_PRINT_STATUS,
    CMD_START_BLE_PROV
} ui_action_t;

// ==============================================================================
// 2. VARIABLES GLOBALES DE ESTADO Y HARDWARE
// ==============================================================================
static rmt_channel_handle_t s_rx_channel = NULL;
static rmt_channel_handle_t s_tx_channel = NULL;
static rmt_encoder_handle_t s_copy_encoder = NULL;

static rmt_receive_config_t s_rx_config = {
    .signal_range_min_ns = 1250,     // 1.25 us: filtro contra glitches
    .signal_range_max_ns = 15000000, // 15 ms: cubre la pausa entre secciones (~7.9ms en DG11J1)
};

static rmt_symbol_word_t s_rx_buffer[MAX_IR_SYMBOLS];
static rmt_symbol_word_t s_tx_buffer[MAX_IR_SYMBOLS];

static QueueHandle_t s_rx_done_queue = NULL;
static QueueHandle_t s_action_queue = NULL;

// Estado centralizado del aire acondicionado
static ac_state_t s_current_state;
static ac_cmd_t   s_last_cmd = AC_CMD_TEMP;

// Callback de interrupción RMT RX (se ejecuta en contexto ISR)
static bool IRAM_ATTR rmt_rx_done_callback(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_data)
{
    BaseType_t high_task_wakeup = pdFALSE;
    size_t count = edata->num_symbols;
    xQueueSendFromISR((QueueHandle_t)user_data, &count, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

// ==============================================================================
// 3. TAREAS DE INTERACCIÓN (BOTÓN BOOT Y CONSOLA)
// ==============================================================================

// Monitorea el botón físico BOOT (GPIO 9, activo en bajo)
// Pulsación corta (<1.5s): Transmite comando actual por IR
// Pulsación larga (>3s): Activa Modo SoftAP para reconfigurar Wi-Fi
static void button_task(void *pvParameters)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    while (1) {
        if (gpio_get_level(BOOT_BUTTON_GPIO) == 0) {
            // Flanco de bajada detectado, debounce de 40ms
            vTaskDelay(pdMS_TO_TICKS(40));
            if (gpio_get_level(BOOT_BUTTON_GPIO) == 0) {
                uint32_t press_ms = 0;
                while (gpio_get_level(BOOT_BUTTON_GPIO) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(50));
                    press_ms += 50;
                    if (press_ms >= 3000) {
                        // Pulsación larga superó los 3 segundos
                        printf("\n[BOTON BOOT] Pulsación larga detectada (>3s) -> Reiniciando Aprovisionamiento BLE...\n");
                        ui_action_t act = CMD_START_BLE_PROV;
                        xQueueSend(s_action_queue, &act, 0);

                        // Esperar a que el usuario suelte el botón para no redisparar
                        while (gpio_get_level(BOOT_BUTTON_GPIO) == 0) {
                            vTaskDelay(pdMS_TO_TICKS(100));
                        }
                        break;
                    }
                }

                // Si se soltó antes de los 3 segundos y duró más de 50ms, es pulsación corta
                if (press_ms > 50 && press_ms < 3000) {
                    ui_action_t act = CMD_SEND_CURRENT;
                    xQueueSend(s_action_queue, &act, 0);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// Monitorea el teclado por consola USB/Serial
static void console_input_task(void *pvParameters)
{
    while (1) {
        int c = getchar();
        if (c != EOF && c > 0) {
            ui_action_t act = CMD_PRINT_STATUS;
            bool valid = true;

            switch (c) {
                case '+':
                case '=':
                    act = CMD_TEMP_UP;
                    break;
                case '-':
                case '_':
                    act = CMD_TEMP_DOWN;
                    break;
                case 'm':
                case 'M':
                    act = CMD_MODE_NEXT;
                    break;
                case 'f':
                case 'F':
                    act = CMD_FAN_NEXT;
                    break;
                case 'p':
                case 'P':
                    act = CMD_POWER_TOGGLE;
                    break;
                case 's':
                case 'S':
                    act = CMD_SWING_TOGGLE;
                    break;
                case 'b':
                case 'B':
                case 'w':
                case 'W':
                    act = CMD_START_BLE_PROV;
                    break;
                case 't':
                case 'T':
                case ' ':
                case '\n':
                case '\r':
                    act = CMD_SEND_CURRENT;
                    break;
                case '?':
                case 'h':
                case 'H':
                    act = CMD_PRINT_STATUS;
                    break;
                default:
                    valid = false;
                    break;
            }

            if (valid) {
                xQueueSend(s_action_queue, &act, 0);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ==============================================================================
// 4. INICIALIZACIÓN DEL SUBSISTEMA RMT
// ==============================================================================
static void init_ir_subsystem(void)
{
    ESP_LOGI(TAG, "Inicializando subsistema RMT...");

    // 1. Canal RX (GPIO 6)
    rmt_rx_channel_config_t rx_channel_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RESOLUTION_HZ,
        .mem_block_symbols = 48,
        .gpio_num = IR_RX_GPIO,
    };
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_channel_cfg, &s_rx_channel));

    s_rx_done_queue = xQueueCreate(2, sizeof(size_t));
    assert(s_rx_done_queue != NULL);

    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = rmt_rx_done_callback,
    };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(s_rx_channel, &cbs, s_rx_done_queue));

    // 2. Canal TX (GPIO 0 con portadora a 38 kHz)
    rmt_tx_channel_config_t tx_channel_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RESOLUTION_HZ,
        .mem_block_symbols = 48,
        .trans_queue_depth = 4,
        .gpio_num = IR_TX_GPIO,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_channel_cfg, &s_tx_channel));

    rmt_carrier_config_t carrier_cfg = {
        .frequency_hz = IR_CARRIER_FREQ_HZ,
        .duty_cycle = 0.50,
    };
    ESP_ERROR_CHECK(rmt_apply_carrier(s_tx_channel, &carrier_cfg));

    rmt_copy_encoder_config_t copy_encoder_cfg = {};
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&copy_encoder_cfg, &s_copy_encoder));

    ESP_ERROR_CHECK(rmt_enable(s_tx_channel));
    ESP_ERROR_CHECK(rmt_enable(s_rx_channel));

    ESP_LOGI(TAG, "RMT listo: TX=GPIO%d (38kHz), RX=GPIO%d", IR_TX_GPIO, IR_RX_GPIO);
}

// Transmite el estado actual hacia el equipo
static void transmit_current_state(void)
{
    uint8_t frame_bytes[DG11J1_STATE_LENGTH];
    dg11j1_build_frame(&s_current_state, s_last_cmd, frame_bytes);

    size_t num_symbols = dg11j1_frame_to_rmt_symbols(frame_bytes, s_tx_buffer, MAX_IR_SYMBOLS);
    if (num_symbols == 0) {
        ESP_LOGE(TAG, "Error construyendo símbolos RMT");
        return;
    }

    printf("\n>>> [TX IR] Transmitiendo comando al Split Marshall...\n");
    dg11j1_print_state(&s_current_state);
    printf("    Comando activo: %s | Símbolos RMT: %u\n", dg11j1_cmd_to_string(s_last_cmd), (unsigned int)num_symbols);

    // Desactivar temporalmente RX para evitar capturar nuestro propio destello
    rmt_disable(s_rx_channel);

    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,
    };
    ESP_ERROR_CHECK(rmt_transmit(s_tx_channel, s_copy_encoder, s_tx_buffer,
                                 num_symbols * sizeof(rmt_symbol_word_t),
                                 &tx_cfg));

    ESP_ERROR_CHECK(rmt_tx_wait_all_done(s_tx_channel, 2000));
    vTaskDelay(pdMS_TO_TICKS(150)); // Disipar reflejos ópticos

    // Reactivar RX
    rmt_enable(s_rx_channel);
    rmt_receive(s_rx_channel, s_rx_buffer, sizeof(s_rx_buffer), &s_rx_config);

    printf(">>> [TX OK] Trama infrarroja enviada con éxito.\n\n");
}

static void print_help_menu(void)
{
    printf("\n===================================================================\n");
    printf("  CONTROLADOR INTELIGENTE AIRE ACONDICIONADO MARSHALL (Fase 3)   \n");
    printf("===================================================================\n");
    printf("  [+] o [=] : Subir temperatura (+1 C)\n");
    printf("  [-] o [_] : Bajar temperatura (-1 C)\n");
    printf("  [m]       : Cambiar modo (Frio -> Calor -> Dry -> Fan -> Auto)\n");
    printf("  [f]       : Cambiar fan  (Auto -> Baja -> Media -> Alta)\n");
    printf("  [p]       : Encender / Apagar (Power Toggle)\n");
    printf("  [s]       : Alternar Swing (Oscilación)\n");
    printf("  [b] / [w] : Reconfigurar Wi-Fi (Modo Aprovisionamiento BLE)\n");
    printf("  [t] / [ENTER] / Botón BOOT (corto): ENVIAR comando IR al aire\n");
    printf("  Botón BOOT (mantener >3 seg)      : Reconfigurar Wi-Fi por BLE\n");
    printf("  [h]       : Ver este menú y estado actual\n");
    printf("-------------------------------------------------------------------\n");
    dg11j1_print_state(&s_current_state);
    printf("-------------------------------------------------------------------\n");
    printf("  [SNIFFER ACTIVO]: Si presionas cualquier botón en tu control físico\n");
    printf("  DG11J1-01 apuntando al TSOP (GPIO %d), se actualizará en tiempo real.\n", IR_RX_GPIO);
    printf("===================================================================\n\n");
}

// ==============================================================================
// 5. RUTINA PRINCIPAL
// ==============================================================================
void app_main(void)
{
    dg11j1_default_state(&s_current_state);
    s_last_cmd = AC_CMD_TEMP;

    s_action_queue = xQueueCreate(10, sizeof(ui_action_t));
    assert(s_action_queue != NULL);

    init_ir_subsystem();

    // Inicializar subsistema Wi-Fi (conecta a NVS o arranca SoftAP de configuración)
    wifi_manager_init();

    xTaskCreate(button_task, "button_task", 2048, NULL, 5, NULL);
    xTaskCreate(console_input_task, "console_task", 2048, NULL, 5, NULL);

    // Iniciar escucha del receptor TSOP
    ESP_ERROR_CHECK(rmt_receive(s_rx_channel, s_rx_buffer, sizeof(s_rx_buffer), &s_rx_config));

    print_help_menu();

    while (1) {
        size_t rx_symbols_count = 0;

        // 1. Escucha de señales del control físico (Sniffing & Decodificación)
        if (xQueueReceive(s_rx_done_queue, &rx_symbols_count, pdMS_TO_TICKS(30)) == pdPASS) {
            if (rx_symbols_count >= 160) {
                ac_state_t decoded_state;
                ac_cmd_t   decoded_cmd;
                if (dg11j1_decode_rmt_symbols(s_rx_buffer, rx_symbols_count, &decoded_state, &decoded_cmd)) {
                    s_current_state = decoded_state;
                    s_last_cmd = decoded_cmd;
                    printf("\n===================================================================\n");
                    printf("  [SYNC REMOTO FISICO] Control remoto DG11J1-01 detectado!\n");
                    printf("  Comando: %s\n", dg11j1_cmd_to_string(decoded_cmd));
                    dg11j1_print_state(&s_current_state);
                    printf("===================================================================\n\n");
                }
            }
            // Rearmar recepción
            ESP_ERROR_CHECK(rmt_receive(s_rx_channel, s_rx_buffer, sizeof(s_rx_buffer), &s_rx_config));
        }

        // 2. Procesamiento de acciones de usuario (Teclado o Botón BOOT)
        ui_action_t action;
        if (xQueueReceive(s_action_queue, &action, 0) == pdPASS) {
            bool changed = false;

            switch (action) {
                case CMD_SEND_CURRENT:
                    transmit_current_state();
                    break;

                case CMD_START_BLE_PROV:
                    wifi_manager_start_ble_provisioning();
                    break;

                case CMD_TEMP_UP:
                    if (s_current_state.temp_c < DG11J1_TEMP_MAX) {
                        s_current_state.temp_c++;
                        s_last_cmd = AC_CMD_TEMP;
                        changed = true;
                    } else {
                        printf("[AVISO] Temperatura máxima alcanzada (30 C)\n");
                    }
                    break;

                case CMD_TEMP_DOWN:
                    if (s_current_state.temp_c > DG11J1_TEMP_MIN) {
                        s_current_state.temp_c--;
                        s_last_cmd = AC_CMD_TEMP;
                        changed = true;
                    } else {
                        printf("[AVISO] Temperatura mínima alcanzada (16 C)\n");
                    }
                    break;

                case CMD_MODE_NEXT:
                    if (s_current_state.mode == AC_MODE_COOL) s_current_state.mode = AC_MODE_HEAT;
                    else if (s_current_state.mode == AC_MODE_HEAT) s_current_state.mode = AC_MODE_DRY;
                    else if (s_current_state.mode == AC_MODE_DRY) s_current_state.mode = AC_MODE_FAN;
                    else if (s_current_state.mode == AC_MODE_FAN) s_current_state.mode = AC_MODE_AUTO;
                    else s_current_state.mode = AC_MODE_COOL;
                    s_last_cmd = AC_CMD_MODE;
                    changed = true;
                    break;

                case CMD_FAN_NEXT:
                    if (s_current_state.fan == AC_FAN_AUTO) s_current_state.fan = AC_FAN_LOW;
                    else if (s_current_state.fan == AC_FAN_LOW) s_current_state.fan = AC_FAN_MED;
                    else if (s_current_state.fan == AC_FAN_MED) s_current_state.fan = AC_FAN_HIGH;
                    else s_current_state.fan = AC_FAN_AUTO;
                    s_last_cmd = AC_CMD_FAN;
                    changed = true;
                    break;

                case CMD_POWER_TOGGLE:
                    s_current_state.power = !s_current_state.power;
                    s_last_cmd = AC_CMD_POWER;
                    changed = true;
                    break;

                case CMD_SWING_TOGGLE:
                    s_current_state.swing = !s_current_state.swing;
                    s_last_cmd = AC_CMD_SWING;
                    changed = true;
                    break;

                case CMD_PRINT_STATUS:
                    print_help_menu();
                    break;
            }

            if (changed) {
                dg11j1_print_state(&s_current_state);
                printf(">> Presiona [t], [ENTER] o el botón BOOT para emitir este estado al aire.\n");
            }
        }
    }
}
