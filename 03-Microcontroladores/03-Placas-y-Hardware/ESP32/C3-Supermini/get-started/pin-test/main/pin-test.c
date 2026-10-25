#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "PIN_TEST";

// Lista de GPIOs presentes en los pines externos del ESP32-C3 Supermini
static const gpio_num_t test_pins[] = {
    GPIO_NUM_0,
    GPIO_NUM_1,
    GPIO_NUM_2,
    GPIO_NUM_3,
    GPIO_NUM_4,
    GPIO_NUM_5,
    GPIO_NUM_6,
    GPIO_NUM_7,
    GPIO_NUM_8,   // LED integrado
    GPIO_NUM_9,   // Botón BOOT (pull-up a 3.3V)
    GPIO_NUM_10,
    GPIO_NUM_20,  // RX estándar
    GPIO_NUM_21   // TX estándar
};

#define NUM_PINS (sizeof(test_pins) / sizeof(test_pins[0]))

void app_main(void)
{
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "  ESP32-C3 Supermini - Todos los Pines en HIGH    ");
    ESP_LOGI(TAG, "==================================================");

    // Preparar máscara de 64 bits para gpio_config
    uint64_t pin_mask = 0;
    for (size_t i = 0; i < NUM_PINS; i++) {
        pin_mask |= (1ULL << test_pins[i]);
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error configurando pines: %s", esp_err_to_name(err));
        return;
    }

    // Poner todos los pines en HIGH (3.3V)
    for (size_t i = 0; i < NUM_PINS; i++) {
        gpio_set_level(test_pins[i], 1);
    }

    ESP_LOGI(TAG, "¡Listo! Los %d pines han sido puestos en HIGH (3.3V).", NUM_PINS);
    ESP_LOGI(TAG, "Se mantendran fijos en HIGH indefinidamente.");
    ESP_LOGI(TAG, "Mide con el multimetro: Punta negra a GND, punta roja a cada pin.");

    uint32_t seconds = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        seconds += 5;
        // Re-aseguramos nivel alto en todos los pines continuamente
        for (size_t i = 0; i < NUM_PINS; i++) {
            gpio_set_level(test_pins[i], 1);
        }
        ESP_LOGI(TAG, "[%lu s] Todos los pines continuan en HIGH (3.3V)", (unsigned long)seconds);
    }
}
