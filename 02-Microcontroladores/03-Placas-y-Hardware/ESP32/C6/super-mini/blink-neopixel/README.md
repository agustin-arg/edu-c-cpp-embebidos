# Control de LED RGB NeoPixel (WS2812) en ESP32-C6 SuperMini

Secuencia cíclica de iluminación (rojo, verde, azul y apagado) para el LED direccionable WS2812 integrado en la placa MakerGo ESP32-C6 SuperMini mediante el periférico RMT y el componente oficial `led_strip` de ESP-IDF.

---

### Paso 1: Inclusión de librerías y asignación del pin GPIO

```c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

#define LED_PIN 8
```

* `freertos/FreeRTOS.h`: Cabecera base del sistema operativo en tiempo real FreeRTOS (tipos y primitivas del kernel).
* `freertos/task.h`: API para control de tareas y retardos temporales.
* `led_strip.h`: Controlador de Espressif para tiras y LEDs direccionables (WS2812 / SK6812).
* `#define`: Directiva de preprocesamiento en C para definir constantes simbólicas.
* `LED_PIN`: Identificador del pin de control de datos.
* `8`: Número del GPIO físico en la ESP32-C6 SuperMini conectado internamente a la línea de datos del WS2812.

> *El **GPIO 8** es un pin de strapping (selección de modo de arranque) en el ESP32-C6. No conectes circuitos pull-down externos pesados a esta línea durante el inicio para evitar bloqueos del procesador.*

---

### Paso 2: Inicialización del periférico RMT y controlador de pixel

```c
led_strip_handle_t led_strip;
led_strip_config_t strip_config = {
    .strip_gpio_num = LED_PIN,
    .max_leds = 1,
};
led_strip_rmt_config_t rmt_config = {
    .resolution_hz = 10 * 1000 * 1000,
};
ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
```

* `led_strip_handle_t`: Puntero opaco que representa la instancia de control del LED.
* `led_strip`: Variable que retiene el manejador devuelto por el driver.
* `led_strip_config_t`: Estructura de configuración general para el periférico LED.
* `strip_config`: Instancia de la estructura de configuración.
* `.strip_gpio_num`: Campo que determina el GPIO de salida de señal de datos.
* `.max_leds`: Cantidad de píxeles encadenados en cascada (`1` para el módulo integrado).
* `led_strip_rmt_config_t`: Estructura de configuración del generador de pulsos RMT (Remote Control Peripheral).
* `rmt_config`: Instancia de la estructura de configuración RMT.
* `.resolution_hz`: Frecuencia de muestreo del periférico RMT (`10 * 1000 * 1000` = 10 MHz, 100 ns por tick, ideal para temporización precisa WS2812).
* `ESP_ERROR_CHECK()`: Macro de ESP-IDF que evalúa códigos `esp_err_t`; aborta con volcado de registros si la inicialización no retorna `ESP_OK`.
* `led_strip_new_rmt_device()`: Función constructora que asigna un canal RMT, inicializa el codificador de símbolos y entrega el dispositivo.

> *Si el sistema aborta arrojando **ESP_ERR_NOT_FOUND**, verifica que no existan otros periféricos agotando los canales RMT de transmisión.*

---

### Paso 3: Bucle cíclico y refresco de color

```c
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
```

* `while (1)`: Bucle permanente de ejecución en FreeRTOS.
* `led_strip_set_pixel()`: Escribe los valores cromáticos en el búfer interno de memoria RAM.
  * Parámetro 1 (`led_strip`): Manejador del dispositivo objetivo.
  * Parámetro 2 (`0`): Índice ordinal del pixel a configurar (índice 0 para el primer LED).
  * Parámetro 3 (`20`): Intensidad canal Rojo (R) en escala de 0 a 255.
  * Parámetro 4 (`0`): Intensidad canal Verde (G) en escala de 0 a 255.
  * Parámetro 5 (`0`): Intensidad canal Azul (B) en escala de 0 a 255.
* `led_strip_refresh()`: Transmite el búfer de colores a través del transmisor RMT hacia el LED físico.
* `vTaskDelay()`: Suspende temporalmente la tarea actual entregando ciclos de reloj al planificador del sistema.
* `pdMS_TO_TICKS()`: Macro que transforma milisegundos en número de ticks del sistema operativo (1000 ms = 1 s).
* `led_strip_clear()`: Apaga todos los leds del strip (pone valores a 0) y actualiza físicamente la señal de salida.

> *La función `led_strip_set_pixel` solo almacena los datos en memoria **RAM local**. El LED no cambiará de color físico hasta que no se invoque obligatoriamente **led_strip_refresh**.*

---

### Paso 4: Compilación y Flasheo con los alias configurados

```bash
t_idf esp32c6
sp_idf /dev/ttyACM1
b_idf
f_idf
```

* `t_idf`: Función inteligente de la sesión que selecciona el microcontrolador destino (`esp32c6`), crea el directorio de compilación `build_esp32c6` y asocia `sdkconfig.esp32c6`.
* `esp32c6`: Parámetro del target compatible con ESP-IDF.
* `sp_idf`: Función que exporta la variable de entorno `ESPPORT` con el puerto serie asignado.
* `/dev/ttyACM1`: Dispositivo serial USB-JTAG asignado al ESP32-C6.
* `b_idf`: Alias que invoca `_idf build` sobre la carpeta y configuración aislada.
* `f_idf`: Alias que invoca `_idf flash` cargando los binarios al microcontrolador.

> *Si el comando **f_idf** falla por permisos en `/dev/ttyACM1`, asegúrate de que tu usuario pertenezca al grupo **uucp** o ejecuta `k_idf` para liberar procesos bloqueando el puerto.*
