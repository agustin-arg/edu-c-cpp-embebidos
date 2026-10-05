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

static const char *TAG = "IR_CONTROLLER";

// ==============================================================================
// 1. DEFINICIÓN DE PINES Y PARÁMETROS DE HARDWARE
// ==============================================================================
#define IR_TX_GPIO          GPIO_NUM_0      // LED Emisor Infrarrojo (con transistor NPN)
#define IR_RX_GPIO          GPIO_NUM_5      // Demodulador TSOP1738 (38 kHz)
#define BOOT_BUTTON_GPIO    GPIO_NUM_9      // Botón BOOT integrado en ESP32-C3 SuperMini

#define IR_RESOLUTION_HZ    1000000         // 1 MHz -> 1 tick = 1 microsegundo (us)
#define IR_CARRIER_FREQ_HZ  38000           // Portadora infrarroja estándar de 38 kHz
#define MAX_IR_SYMBOLS      600             // Capacidad para ráfagas HVAC completas (DG11J1 usa ~250-350)

// Tipos de disparador para la retransmisión
typedef enum {
    TRIGGER_NONE = 0,
    TRIGGER_STDIN,
    TRIGGER_BOOT_BUTTON
} trigger_source_t;

// ==============================================================================
// 2. VARIABLES DE ESTADO Y COLAS
// ==============================================================================
static rmt_channel_handle_t s_rx_channel = NULL;
static rmt_channel_handle_t s_tx_channel = NULL;
static rmt_encoder_handle_t s_copy_encoder = NULL;

static rmt_receive_config_t s_rx_config = {
    .signal_range_min_ns = 1250,     // 1.25 us: filtro de hardware para glitches (límite máx en C3: 3187 ns)
    .signal_range_max_ns = 15000000, // 15 ms: cubre la pausa entre secciones (~7.9ms en DG11J1)
};

static rmt_symbol_word_t s_rx_buffer[MAX_IR_SYMBOLS];
static rmt_symbol_word_t s_recorded_symbols[MAX_IR_SYMBOLS];
static size_t s_recorded_symbol_count = 0;
static bool s_has_recorded_signal = false;

static QueueHandle_t s_rx_done_queue = NULL;
static QueueHandle_t s_trigger_queue = NULL;

// Callback de interrupción RMT RX (se ejecuta en contexto ISR)
static bool IRAM_ATTR rmt_rx_done_callback(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_data)
{
    BaseType_t high_task_wakeup = pdFALSE;
    size_t count = edata->num_symbols;
    xQueueSendFromISR((QueueHandle_t)user_data, &count, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

// ==============================================================================
// 3. TAREAS DE INTERACCIÓN: BOTÓN BOOT Y ENTRADA DE CONSOLA (STDIN)
// ==============================================================================

// Monitorea el botón físico BOOT (GPIO 9, activo en bajo)
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

    int last_level = 1;
    while (1) {
        int current_level = gpio_get_level(BOOT_BUTTON_GPIO);
        if (last_level == 1 && current_level == 0) {
            // Flanco de bajada (botón presionado): debounce de 35ms
            vTaskDelay(pdMS_TO_TICKS(35));
            if (gpio_get_level(BOOT_BUTTON_GPIO) == 0) {
                trigger_source_t trig = TRIGGER_BOOT_BUTTON;
                xQueueSend(s_trigger_queue, &trig, 0);
            }
        }
        last_level = current_level;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// Monitorea el teclado en la terminal serial USB (ENTER, Espacio o 's')
static void console_input_task(void *pvParameters)
{
    while (1) {
        int c = getchar();
        if (c != EOF && c > 0) {
            if (c == '\n' || c == '\r' || c == ' ' || c == 's' || c == 'S') {
                trigger_source_t trig = TRIGGER_STDIN;
                xQueueSend(s_trigger_queue, &trig, 0);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ==============================================================================
// 4. INICIALIZACIÓN DEL SUBSISTEMA RMT (TX Y RX)
// ==============================================================================
static void init_ir_subsystem(void)
{
    ESP_LOGI(TAG, "Inicializando subsistema RMT en ESP-IDF v5.5...");

    // 1. Canal RX (Receptor TSOP1738 en GPIO 5)
    rmt_rx_channel_config_t rx_channel_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RESOLUTION_HZ,
        .mem_block_symbols = 48, // 1 bloque completo de 48 palabras en ESP32-C3
        .gpio_num = IR_RX_GPIO,
    };
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_channel_cfg, &s_rx_channel));

    s_rx_done_queue = xQueueCreate(2, sizeof(size_t));
    assert(s_rx_done_queue != NULL);

    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = rmt_rx_done_callback,
    };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(s_rx_channel, &cbs, s_rx_done_queue));

    // 2. Canal TX (Emisor LED IR en GPIO 0)
    rmt_tx_channel_config_t tx_channel_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RESOLUTION_HZ,
        .mem_block_symbols = 48, // 1 bloque de 48 palabras en ESP32-C3
        .trans_queue_depth = 4,
        .gpio_num = IR_TX_GPIO,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_channel_cfg, &s_tx_channel));

    // Modulación por portadora de 38 kHz para el emisor
    rmt_carrier_config_t carrier_cfg = {
        .frequency_hz = IR_CARRIER_FREQ_HZ,
        .duty_cycle = 0.33, // Duty cycle óptimo para emisión infrarroja
    };
    ESP_ERROR_CHECK(rmt_apply_carrier(s_tx_channel, &carrier_cfg));

    // Codificador de copia directa para emitir secuencias de símbolos crudas
    rmt_copy_encoder_config_t copy_encoder_cfg = {};
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&copy_encoder_cfg, &s_copy_encoder));

    // Habilitar canales
    ESP_ERROR_CHECK(rmt_enable(s_tx_channel));
    ESP_ERROR_CHECK(rmt_enable(s_rx_channel));

    ESP_LOGI(TAG, "Hardware RMT TX y RX configurado con éxito.");
}

// ==============================================================================
// 5. RUTINA PRINCIPAL DE APLICACIÓN
// ==============================================================================
void app_main(void)
{
    printf("\n");
    printf("===================================================================\n");
    printf("  CONTROLADOR DE AIRE ACONDICIONADO MARSHALL (ESP32-C3 SuperMini)  \n");
    printf("  Modo: Sniff & Replay Crudo con RMT (Opción 1)                   \n");
    printf("  Receptor TSOP1738 : GPIO %d                                       \n", IR_RX_GPIO);
    printf("  Emisor LED IR     : GPIO %d (38 kHz)                              \n", IR_TX_GPIO);
    printf("  Botón BOOT        : GPIO %d                                       \n", BOOT_BUTTON_GPIO);
    printf("  Control objetivo  : DG11J1-01 (Industria Argentina / Whirlpool)  \n");
    printf("===================================================================\n\n");

    s_trigger_queue = xQueueCreate(4, sizeof(trigger_source_t));
    assert(s_trigger_queue != NULL);

    init_ir_subsystem();

    // Iniciar tareas de captura de botón y entrada de consola
    xTaskCreate(button_task, "button_task", 2048, NULL, 5, NULL);
    xTaskCreate(console_input_task, "console_task", 2048, NULL, 5, NULL);

    // Iniciar primera recepción en segundo plano
    ESP_ERROR_CHECK(rmt_receive(s_rx_channel, s_rx_buffer, sizeof(s_rx_buffer), &s_rx_config));
    printf("[ESCUDANDO] Apunta tu control remoto DG11J1-01 al receptor TSOP (GPIO %d)\n", IR_RX_GPIO);
    printf("            y presiona el botón de ENCENDIDO (o cualquier función)...\n\n");

    while (1) {
        size_t rx_symbols_count = 0;

        // 1. Comprobar si llegó una señal por el TSOP
        if (xQueueReceive(s_rx_done_queue, &rx_symbols_count, pdMS_TO_TICKS(40)) == pdPASS) {
            // Filtrar ruidos espurios muy cortos (< 10 símbolos)
            if (rx_symbols_count < 10) {
                ESP_ERROR_CHECK(rmt_receive(s_rx_channel, s_rx_buffer, sizeof(s_rx_buffer), &s_rx_config));
                continue;
            }

            s_recorded_symbol_count = rx_symbols_count;
            if (s_recorded_symbol_count > MAX_IR_SYMBOLS) {
                s_recorded_symbol_count = MAX_IR_SYMBOLS;
            }

            // Normalizar símbolos para la transmisión:
            // En el TSOP: nivel bajo = ráfaga 38 kHz (MARK), nivel alto = silencio (SPACE)
            // Para el emisor RMT: level0 = 1 (emite 38 kHz), level1 = 0 (silencio)
            uint32_t total_us = 0;
            for (size_t i = 0; i < s_recorded_symbol_count; i++) {
                s_recorded_symbols[i].duration0 = s_rx_buffer[i].duration0;
                s_recorded_symbols[i].level0 = 1; // Ráfaga activa 38 kHz
                s_recorded_symbols[i].duration1 = s_rx_buffer[i].duration1;
                s_recorded_symbols[i].level1 = 0; // Silencio
                total_us += s_recorded_symbols[i].duration0 + s_recorded_symbols[i].duration1;
            }
            s_has_recorded_signal = true;

            printf("\n-------------------------------------------------------------------\n");
            printf("  [CAPTURA EXITOSA] Señal grabada en memoria RAM!\n");
            printf("  Total de símbolos recibidos: %u\n", (unsigned int)s_recorded_symbol_count);
            printf("  Duración total aproximada   : %lu ms\n", (unsigned long)(total_us / 1000));
            printf("  Cabecera detectada          : Mark = %u us | Space = %u us\n",
                   (unsigned int)s_recorded_symbols[0].duration0,
                   (unsigned int)s_recorded_symbols[0].duration1);

            // Diagnóstico rápido de protocolo DG11J1 / Whirlpool:
            if (s_recorded_symbols[0].duration0 >= 7500 && s_recorded_symbols[0].duration0 <= 10500) {
                printf("  >> NOTA: Cabecera compatible con protocolo DG11J1 / Whirlpool (~9ms) <<\n");
            }

            printf("-------------------------------------------------------------------\n");
            printf(">>> LISTO PARA PROBAR EN EL AIRE ACONDICIONADO: <<<\n");
            printf("  1. Apunta el LED IR (GPIO %d) hacia la unidad split de tu Marshall.\n", IR_TX_GPIO);
            printf("  2. Presiona el botón físico BOOT en la placa ESP32-C3 SuperMini\n");
            printf("     O pulsa la tecla ENTER (o Espacio) en esta consola serial.\n");
            printf("-------------------------------------------------------------------\n\n");

            // Rearmar la escucha del receptor para permitir re-grabar cuando se desee
            ESP_ERROR_CHECK(rmt_receive(s_rx_channel, s_rx_buffer, sizeof(s_rx_buffer), &s_rx_config));
        }

        // 2. Comprobar si el usuario ordenó la retransmisión
        trigger_source_t trigger_src = TRIGGER_NONE;
        if (xQueueReceive(s_trigger_queue, &trigger_src, 0) == pdPASS) {
            if (!s_has_recorded_signal) {
                printf("\n[AVISO] Todavía no has capturado ninguna señal del control.\n");
                printf("        Apunta primero tu control remoto al TSOP (GPIO %d) y presiona un botón.\n\n", IR_RX_GPIO);
            } else {
                printf("\n>>> [%s] Disparando transmisión por LED IR (GPIO %d)... <<<\n",
                       trigger_src == TRIGGER_BOOT_BUTTON ? "BOTON FISICO BOOT" : "TECLA ENTER EN CONSOLA",
                       IR_TX_GPIO);

                // Deshabilitar temporalmente RX para evitar capturar nuestro propio destello
                rmt_disable(s_rx_channel);

                rmt_transmit_config_t tx_cfg = {
                    .loop_count = 0,
                };
                ESP_ERROR_CHECK(rmt_transmit(s_tx_channel, s_copy_encoder, s_recorded_symbols,
                                             s_recorded_symbol_count * sizeof(rmt_symbol_word_t),
                                             &tx_cfg));

                // Esperar a que la transmisión óptica concluya
                ESP_ERROR_CHECK(rmt_tx_wait_all_done(s_tx_channel, 2000));

                // Pequeña pausa de seguridad para disipar reflejos en la habitación
                vTaskDelay(pdMS_TO_TICKS(150));

                // Reactivar receptor TSOP
                rmt_enable(s_rx_channel);
                rmt_receive(s_rx_channel, s_rx_buffer, sizeof(s_rx_buffer), &s_rx_config);

                printf(">>> [OK] Señal transmitida al split.\n");
                printf("    - ¿El aire acondicionado respondió (hizo beep / encendió / cambió)?\n");
                printf("    - Puedes presionar BOOT o ENTER nuevamente para reenviar el mismo comando.\n");
                printf("    - O presionar otro botón en tu control remoto frente al TSOP para grabar uno nuevo.\n\n");
            }
        }
    }
}
