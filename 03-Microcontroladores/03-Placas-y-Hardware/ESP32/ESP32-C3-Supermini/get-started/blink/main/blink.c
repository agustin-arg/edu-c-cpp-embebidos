#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

#define LED_PIN_5 GPIO_NUM_5
#define LED_PIN_0 GPIO_NUM_0

void app_main(void)
{
    gpio_reset_pin(LED_PIN_5);
    gpio_set_direction(LED_PIN_5, GPIO_MODE_OUTPUT);
    
    gpio_reset_pin(LED_PIN_0);
    gpio_set_direction(LED_PIN_0, GPIO_MODE_OUTPUT);

    while (1)
    {
        printf("LED ON\n");

        gpio_set_level(LED_PIN_5, 1);
        gpio_set_level(LED_PIN_0, 1);
        vTaskDelay(pdMS_TO_TICKS(500));

        printf("LED OFF\n");

        gpio_set_level(LED_PIN_5, 0);
        gpio_set_level(LED_PIN_0, 0);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
