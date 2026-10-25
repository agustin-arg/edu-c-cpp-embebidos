#include <stdio.h>
#include "esp_log.h"
#include "lvgl.h"
#include "ui.h"

void app_main(void)
{
// 1. Inicializar los pines y el bus RGB de la pantalla de 7"
bsp_display_init();

// 2. Inicializar el driver del touch (GT911)
bsp_touch_init();

// 3. Inicializar la librería LVGL
lv_init();

// 4. Cargar la interfaz que diseñaste en EEZ Studio:
ui_init();

// 5. El bucle de refresco de LVGL (suele correr en una tarea de FreeRTOS)
while (1) {
lv_timer_handler();  // Atiende eventos, animaciones y refresco
ui_tick();           // Ticks internos de EEZ Studio
vTaskDelay(pdMS_TO_TICKS(10));
}
}
