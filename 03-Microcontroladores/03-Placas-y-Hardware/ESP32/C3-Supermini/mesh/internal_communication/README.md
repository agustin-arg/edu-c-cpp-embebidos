# Mesh Internal Communication — Adaptación para ESP32-C3 Supermini

Este proyecto implementa el ejemplo estándar de **ESP-WIFI-MESH** (`internal_communication`) de Espressif, adaptado con los **cambios mínimos y necesarios** para funcionar de manera estable en el módulo **ESP32-C3 Supermini**.

---

## 1. El Problema Original en el ESP32-C3 Supermini

Al compilar y ejecutar el ejemplo oficial sin modificar en el ESP32-C3 Supermini, el nodo se elegía como Root pero **nunca lograba conectarse al router**, cayendo en un bucle infinito de desconexión:

```text
wifi:state: init -> auth (0xb0)
wifi:state: auth -> init (0x200)
mesh_main: <MESH_EVENT_PARENT_DISCONNECTED>reason:2 (auth expire)
```

El nodo intentaba autenticarse cada pocos segundos y el router rechazaba silenciosamente el paquete (timeout de autenticación a los 1000 ms).

---

## 2. Modificaciones Mínimas Aplicadas

Para mantener la integridad del ejemplo original, solo se comentó lo que generaba conflicto y se agregaron las líneas estrictamente indispensables:

### A. Deshabilitar control de LED en pines de hardware (`main/mesh_light.c`)
* **Qué se modificó:** Se comentó con `#if 0 ... #endif` el bloque de inicialización de LEDC PWM en `mesh_light_init()` y se dejó `mesh_light_set()` retornando `ESP_OK`.
* **Motivo:** El ejemplo original fue diseñado para la placa *ESP-WROVER-KIT* y configuraba salidas PWM en `GPIO 0, 2, 4 y 5`. En el **ESP32-C3**, el **`GPIO 2` es un pin de *strapping* y calibración de RF**. Inyectarle una señal PWM de 5 kHz generaba interferencias directas sobre el circuito sintonizador de la antena durante la asociación Wi-Fi.

### B. Ajustes de enlace Wi-Fi STA en `app_main()` (`main/mesh_main.c`)
Se agregaron únicamente **12 líneas** inmediatamente después de `ESP_ERROR_CHECK(esp_mesh_set_config(&cfg));`:

```c
/* Adaptaciones mínimas necesarias para ESP32-C3 Supermini:
 * 1. Habilitar PMF para routers modernos con WPA2/WPA3 mixto (evita auth expire).
 * 2. Desactivar modem sleep (evita pérdida de paquetes en el C3).
 * 3. Ajustar potencia a 15 dBm para estabilidad del regulador de la placa. */
wifi_config_t sta_cfg;
if (esp_wifi_get_config(WIFI_IF_STA, &sta_cfg) == ESP_OK) {
    sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    sta_cfg.sta.pmf_cfg.capable = true;
    sta_cfg.sta.pmf_cfg.required = false;
    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
}
esp_wifi_set_ps(WIFI_PS_NONE);
esp_wifi_set_max_tx_power(60);
```

### C. Ajustes de Configuración (`sdkconfig` y `sdkconfig.defaults`)
1. **Consola por USB Serial/JTAG:** `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`.  
   Permite ver la consola a través del puerto USB-C nativo del Supermini (`/dev/ttyACM0`) sin depender de UART0 (GPIO 20/21).
2. **Canal fijado:** `CONFIG_MESH_CHANNEL=11` (coincidente con el canal del router en 2.4 GHz).

---

## 3. Comparativa: ¿Por qué funcionó directo en el S3 y en el C6 pero no en el C3 Supermini?

| Factor | ESP32-S3 (Touch-LCD-7") | ESP32-C6 (LCD-1.47") | ESP32-C3 Supermini |
| :--- | :--- | :--- | :--- |
| **Generación Wi-Fi** | Wi-Fi 4 (802.11 b/g/n) | **Wi-Fi 6 (802.11 ax)** | Wi-Fi 4 (802.11 b/g/n) |
| **Soporte PMF en router mixto** | Negociado por pila dual-core | **Obligatorio por Wi-Fi 6** (automático por hardware MAC) | **Requiere declararse explícito** (`pmf_cfg.capable=true`), sino el router descarta con `reason:2` |
| **Pines usados por `mesh_light` (GPIO 0, 2, 4, 5)** | GPIOs de propósito general sin función crítica | Strapping en otros pines (8, 9, 15) | **GPIO 2 es strapping y sintonización de RF**. El PWM afectaba la antena |
| **Etapa de alimentación (LDO)** | Regulador robusto (800–1000 mA) con capacitores para pantalla | Regulador con filtrado para display y backlight | **Mini-LDO ultracompacto** (150–250 mA). Picos de 20 dBm en AP+STA causaban micro-caídas |
| **Módem Sleep en Mesh** | Soportado con amplio margen de corriente | Gestión PMU avanzada en hardware C6 | Caídas de tensión al despertar de sleep; requiere `WIFI_PS_NONE` |

---

## 4. Compilación y Uso

```bash
# 1. Compilar el proyecto
idf.py build

# 2. Flashear al ESP32-C3 Supermini
ESPPORT=/dev/ttyACM0 idf.py flash

# 3. Monitorear los mensajes en tiempo real
ESPPORT=/dev/ttyACM0 idf.py monitor
```

### Salida esperada en el monitor:

```text
W (13544) mesh_main: [#RX:10/10][L:1] parent:08:7e:64:42:fc:10, receive from 0c:4e:a0:66:f2:cc, size:1460, heap:155856, flag:0[err:0x0, proto:0, tos:0]
W (14544) mesh_main: [#RX:11/11][L:1] parent:08:7e:64:42:fc:10, receive from 0c:4e:a0:66:f2:cc, size:1460, heap:155856, flag:0[err:0x0, proto:0, tos:0]
```

* **`L:1`**: Nodo auto-elegido como Root (Capa 1).
* **`parent: 08:7e:64:42:fc:10`**: Conectado directamente a tu router.
* **`#RX: X/X`**: Paquetes P2P de 1460 bytes transmitidos y recibidos con 0% de pérdidas.
