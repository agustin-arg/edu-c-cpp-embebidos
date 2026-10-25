#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "IR_TEST";

#define IR_TX_GPIO GPIO_NUM_0  // Emisor: LED Infrarrojo
#define IR_RX_GPIO GPIO_NUM_5  // Receptor: Sensor TSOP1738

// Configuración LEDC para portadora de 38 kHz
#define LEDC_TIMER        LEDC_TIMER_0
#define LEDC_MODE         LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL      LEDC_CHANNEL_0
#define LEDC_DUTY_RES     LEDC_TIMER_8_BIT
#define LEDC_DUTY_50      (128)
#define LEDC_FREQUENCY    (38000) // 38 kHz

static volatile bool s_burst_mode_enabled = false;
static volatile bool s_carrier_state = false;
static volatile uint32_t s_pulse_count = 0;

static void ir_set_carrier(bool on)
{
    if (on) {
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, LEDC_DUTY_50);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    } else {
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    }
}

// Timer de modulación: genera ráfagas de 800us ON / 800us OFF (requerido por el filtro AGC del TSOP)
static void ir_modulation_timer_callback(void* arg)
{
    if (!s_burst_mode_enabled) {
        if (s_carrier_state) {
            ir_set_carrier(false);
            s_carrier_state = false;
        }
        return;
    }

    s_carrier_state = !s_carrier_state;
    ir_set_carrier(s_carrier_state);
}

// Interrupción por flanco de bajada del TSOP1738 (activo en nivel bajo cuando recibe 38kHz)
static void IRAM_ATTR gpio_rx_isr(void* arg)
{
    s_pulse_count++;
}

static void init_hardware(void)
{
    // 1. Configurar LEDC para el emisor IR (GPIO 0 a 38 kHz)
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .duty_resolution  = LEDC_DUTY_RES,
        .timer_num        = LEDC_TIMER,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = IR_TX_GPIO,
        .duty           = 0,
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    // 2. Configurar temporizador de modulación (ráfagas cada 800 microsegundos)
    const esp_timer_create_args_t timer_args = {
        .callback = &ir_modulation_timer_callback,
        .name = "ir_mod_timer"
    };
    esp_timer_handle_t timer_handle;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer_handle));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer_handle, 800)); // 800 us

    // 3. Configurar pin receptor TSOP1738 (GPIO 5)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << IR_RX_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(IR_RX_GPIO, gpio_rx_isr, NULL));
}

void app_main(void)
{
    ESP_LOGI(TAG, "============================================================");
    ESP_LOGI(TAG, "  Test de Sensor Infrarrojo TSOP1738 + LED Emisor 38 kHz    ");
    ESP_LOGI(TAG, "  Emisor IR: GPIO %d | Receptor TSOP: GPIO %d               ", IR_TX_GPIO, IR_RX_GPIO);
    ESP_LOGI(TAG, "============================================================");

    init_hardware();

    ESP_LOGI(TAG, "Hardware inicializado. Comenzando lecturas constantes...\n");

    int cycle_counter = 0;
    const int TOGGLE_INTERVAL = 15; // Alterna emisor cada 15 lecturas (~2.2 segundos)

    while (1) {
        // Alternar el emisor IR
        if (cycle_counter % TOGGLE_INTERVAL == 0) {
            s_burst_mode_enabled = !s_burst_mode_enabled;
            if (s_burst_mode_enabled) {
                printf("\n>>> [EMISOR IR ACTIVADO] Enviando rafagas a 38 kHz <<<\n");
            } else {
                printf("\n--- [EMISOR IR APAGADO] Silencio infrarrojo ---\n");
            }
        }
        cycle_counter++;

        // Pequeña espera para capturar muestras
        vTaskDelay(pdMS_TO_TICKS(150));

        // Tomar muestra de pulsos y nivel del pin
        uint32_t pulses = s_pulse_count;
        s_pulse_count = 0;
        int raw_pin = gpio_get_level(IR_RX_GPIO);

        // TSOP1738 es activo en bajo (0V cuando detecta 38kHz, 3.3V en reposo)
        int detected = (pulses > 0 || raw_pin == 0) ? 1 : 0;

        printf("[SENSOR GPIO %d] Detectado: %d | Pin RAW: %d | Pulsos: %2lu | Emisor (GPIO %d): %s\n",
               IR_RX_GPIO,
               detected,
               raw_pin,
               (unsigned long)pulses,
               IR_TX_GPIO,
               s_burst_mode_enabled ? "EMITIENDO [ON] " : "APAGADO  [OFF]");
    }
}
