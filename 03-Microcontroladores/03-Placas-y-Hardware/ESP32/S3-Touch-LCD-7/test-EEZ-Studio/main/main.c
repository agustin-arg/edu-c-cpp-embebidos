#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Driver de hardware de pantalla y adaptador LVGL
#include "waveshare_rgb_lcd_port.h"
#include "esp_lv_adapter.h"

// Interfaz generada por EEZ Studio
#include "ui.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "Iniciando Waveshare ESP32-S3 Touch LCD 7 con EEZ Studio...");

    // 1. Inicializar pantalla, backlight, táctil y LVGL
    ESP_ERROR_CHECK(bsp_display_start());

    // 2. Cargar la interfaz de EEZ Studio (protegida por el mutex del adaptador)
    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        ESP_LOGI(TAG, "Cargando pantalla de EEZ Studio...");
        ui_init();
        esp_lv_adapter_unlock();
    }

    ESP_LOGI(TAG, "¡Sistema iniciado! Pantalla lista.");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
