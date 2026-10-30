/* Mesh Internal Communication - Adaptado para ESP32-C3 Supermini */

#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_mesh.h"
#include "mesh_light.h"
#include "driver/gpio.h"

static const char *TAG = "mesh_light";

// En el ESP32-C3 Supermini, el LED azul integrado está en GPIO 8 (activo a nivel bajo)
#define ONBOARD_LED_GPIO GPIO_NUM_8

static bool s_light_inited = false;

esp_err_t mesh_light_init(void)
{
    if (s_light_inited) {
        return ESP_OK;
    }
    s_light_inited = true;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << ONBOARD_LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    // Apagado por defecto (nivel alto en el Supermini)
    gpio_set_level(ONBOARD_LED_GPIO, 1);

    ESP_LOGI(TAG, "LED de estado de placa inicializado en GPIO %d", ONBOARD_LED_GPIO);
    return ESP_OK;
}

esp_err_t mesh_light_set(int color)
{
    if (!s_light_inited) {
        mesh_light_init();
    }

    if (color == 0) {
        gpio_set_level(ONBOARD_LED_GPIO, 1); // Apagado
    } else {
        gpio_set_level(ONBOARD_LED_GPIO, 0); // Encendido
    }
    return ESP_OK;
}

void mesh_connected_indicator(int layer)
{
    ESP_LOGI(TAG, "Indicador MESH: Conectado en Capa (Layer) %d", layer);
    gpio_set_level(ONBOARD_LED_GPIO, 0); // Enciende LED
}

void mesh_disconnected_indicator(void)
{
    ESP_LOGW(TAG, "Indicador MESH: Desconectado");
    gpio_set_level(ONBOARD_LED_GPIO, 1); // Apaga LED
}

esp_err_t mesh_light_process(mesh_addr_t *from, uint8_t *buf, uint16_t len)
{
    mesh_light_ctl_t *in = (mesh_light_ctl_t *) buf;
    if (!from || !buf || len < sizeof(mesh_light_ctl_t)) {
        return ESP_FAIL;
    }
    if (in->token_id != MESH_TOKEN_ID || in->token_value != MESH_TOKEN_VALUE) {
        return ESP_FAIL;
    }
    if (in->cmd == MESH_CONTROL_CMD) {
        if (in->on) {
            mesh_connected_indicator(esp_mesh_get_layer());
        } else {
            mesh_light_set(0);
        }
    }
    return ESP_OK;
}
