#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

#define LED_PIN 21

void app_main(void)
{
    led_strip_handle_t led_strip;
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_PIN,
        .max_leds = 1,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));

    while (1) {
        led_strip_set_pixel(led_strip, 0, 20, 0, 0);
        led_strip_refresh(led_strip);
        vTaskDelay(pdMS_TO_TICKS(1000));

        led_strip_set_pixel(led_strip, 0, 0, 20, 0);
        led_strip_refresh(led_strip);
        vTaskDelay(pdMS_TO_TICKS(1000));

        led_strip_set_pixel(led_strip, 0, 0, 0, 20);
        led_strip_refresh(led_strip);
        vTaskDelay(pdMS_TO_TICKS(1000));

        led_strip_clear(led_strip);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
