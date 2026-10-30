#include "led_indicator.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "LED_IND";

#define LEDC_TIMER_NUM          LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_CH_NUM             LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_10_BIT
#define LEDC_DUTY_MAX           ((1 << LEDC_TIMER_10_BIT) - 1) // 1023
#define LEDC_FREQUENCY_HZ       5000

typedef enum {
    LED_CMD_SET_MODE,
    LED_CMD_TRIGGER_EVENT,
} led_cmd_type_t;

typedef struct {
    led_cmd_type_t type;
    union {
        led_mode_t mode;
        led_event_t event;
    };
} led_cmd_t;

static QueueHandle_t s_led_queue = NULL;
static led_mode_t s_base_mode = LED_MODE_OFF;

static void led_fade_to(uint32_t duty, int duration_ms)
{
    ledc_set_fade_with_time(LEDC_MODE, LEDC_CH_NUM, duty, duration_ms);
    ledc_fade_start(LEDC_MODE, LEDC_CH_NUM, LEDC_FADE_WAIT_DONE);
}

static void led_set_duty_direct(uint32_t duty)
{
    ledc_set_duty_and_update(LEDC_MODE, LEDC_CH_NUM, duty, 0);
}

static void play_ir_tx_animation(void)
{
    // Ráfaga rápida difuminada (3 destellos rápidos de 40 ms)
    for (int i = 0; i < 3; i++) {
        led_fade_to(LEDC_DUTY_MAX, 35);
        led_fade_to(0, 35);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void play_ir_rx_animation(void)
{
    // Doble pulso suave difuminado a velocidad media (100 ms cada uno)
    for (int i = 0; i < 2; i++) {
        led_fade_to(LEDC_DUTY_MAX, 100);
        led_fade_to(0, 100);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void play_wifi_connected_animation(void)
{
    // Pulso suave de confirmación de conexión (250 ms subida, 250 ms bajada)
    led_fade_to(LEDC_DUTY_MAX, 250);
    led_fade_to(0, 250);
}

static void led_indicator_task(void *pvParameters)
{
    led_cmd_t cmd;

    while (1) {
        if (s_base_mode == LED_MODE_OFF) {
            // En reposo, asegurar LED apagado y esperar bloqueado hasta que llegue orden
            led_set_duty_direct(0);

            if (xQueueReceive(s_led_queue, &cmd, portMAX_DELAY) == pdTRUE) {
                if (cmd.type == LED_CMD_SET_MODE) {
                    s_base_mode = cmd.mode;
                } else if (cmd.type == LED_CMD_TRIGGER_EVENT) {
                    switch (cmd.event) {
                        case LED_EVENT_IR_TX:
                            play_ir_tx_animation();
                            break;
                        case LED_EVENT_IR_RX:
                            play_ir_rx_animation();
                            break;
                        case LED_EVENT_WIFI_CONNECTED:
                            play_wifi_connected_animation();
                            break;
                    }
                    led_set_duty_direct(0);
                }
            }
        } else if (s_base_mode == LED_MODE_PROVISIONING) {
            // Modo respiración continua (fade 0 -> MAX en 700ms, luego MAX -> 0 en 700ms)
            led_fade_to(LEDC_DUTY_MAX, 700);

            if (xQueueReceive(s_led_queue, &cmd, pdMS_TO_TICKS(50)) == pdTRUE) {
                goto handle_cmd;
            }

            led_fade_to(0, 700);

            if (xQueueReceive(s_led_queue, &cmd, pdMS_TO_TICKS(200)) == pdTRUE) {
                goto handle_cmd;
            }

            continue;

handle_cmd:
            if (cmd.type == LED_CMD_SET_MODE) {
                s_base_mode = cmd.mode;
            } else if (cmd.type == LED_CMD_TRIGGER_EVENT) {
                switch (cmd.event) {
                    case LED_EVENT_IR_TX:
                        play_ir_tx_animation();
                        break;
                    case LED_EVENT_IR_RX:
                        play_ir_rx_animation();
                        break;
                    case LED_EVENT_WIFI_CONNECTED:
                        play_wifi_connected_animation();
                        break;
                }
            }
            if (s_base_mode == LED_MODE_OFF) {
                led_set_duty_direct(0);
            }
        }
    }
}

void led_indicator_init(void)
{
    ESP_LOGI(TAG, "Inicializando LED Indicator (GPIO %d, active_%s, PWM 5 kHz)...",
             STATUS_LED_GPIO, STATUS_LED_ACTIVE_LOW ? "LOW" : "HIGH");

    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = LEDC_DUTY_RES,
        .timer_num = LEDC_TIMER_NUM,
        .freq_hz = LEDC_FREQUENCY_HZ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    ledc_channel_config_t channel_conf = {
        .gpio_num = STATUS_LED_GPIO,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CH_NUM,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_NUM,
        .duty = 0,
        .hpoint = 0,
        .flags = {
            .output_invert = STATUS_LED_ACTIVE_LOW ? 1 : 0
        }
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_conf));

    // Instalar driver de desvanecimiento por hardware
    ESP_ERROR_CHECK(ledc_fade_func_install(0));

    // Asegurar LED inicialmente apagado
    led_set_duty_direct(0);

    // Cola de eventos (capacidad 8 elementos)
    s_led_queue = xQueueCreate(8, sizeof(led_cmd_t));

    // Crear tarea en segundo plano
    xTaskCreate(led_indicator_task, "led_indicator_task", 3072, NULL, 3, NULL);

    ESP_LOGI(TAG, "LED Indicator listo.");
}

void led_indicator_set_mode(led_mode_t mode)
{
    if (!s_led_queue) return;
    led_cmd_t cmd = {
        .type = LED_CMD_SET_MODE,
        .mode = mode
    };
    xQueueSend(s_led_queue, &cmd, 0);
}

void led_indicator_trigger(led_event_t evt)
{
    if (!s_led_queue) return;
    led_cmd_t cmd = {
        .type = LED_CMD_TRIGGER_EVENT,
        .event = evt
    };
    xQueueSend(s_led_queue, &cmd, 0);
}
