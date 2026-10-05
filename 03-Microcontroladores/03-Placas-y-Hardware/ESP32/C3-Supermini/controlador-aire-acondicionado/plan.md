# Plan de Proyecto: Controlador Inteligente de Aire Acondicionado Marshall

## 1. Resumen Ejecutivo
Desarrollo de un firmware nativo en **ESP-IDF v5.5** para el microcontrolador **ESP32-C3 SuperMini**, capaz de controlar un acondicionador de aire marca **Marshall** mediante señales infrarrojas (IR). El sistema permitirá control autónomo por horarios y la integración en red local con una **Raspberry Pi 4 B** (mediante Home Assistant y MQTT), además de una API REST HTTP local.

---

## 2. Hardware y Conexiones Actuales

### 2.1 Componentes en Uso
- **Microcontrolador**: ESP32-C3 SuperMini (RISC-V 32-bit a 160 MHz, Wi-Fi 4, BLE 5).
- **Emisor IR**: Diodo LED Infrarrojo de 940 nm excitado mediante transistor NPN (para suministrar la corriente necesaria para alcance óptico).
- **Receptor IR**: Demodulador TSOP1738 (38 kHz, activo en nivel bajo).
- **Control Remoto Físico**: Modelo **DG11J1-01** (Industria Argentina).
- **Equipo a Controlar**: Aire Acondicionado Split marca **Marshall**.

### 2.2 Asignación de Pines (GPIO)
Las conexiones probadas y verificadas funcionalmente en `/home/agust/Projects/ESP32/C3-Supermini/get-started/ir-test` son:
- **GPIO 0**: Salida TX (Emisor LED Infrarrojo con portadora de 38 kHz).
- **GPIO 5**: Entrada RX (Receptor TSOP1738 con pull-up interno / filtro).
- **Alimentación**: 5V / 3.3V y GND común.

---

## 3. Protocolo Infrarrojo del Control Remoto (DG11J1-01)
El control remoto físico **DG11J1-01** corresponde a la arquitectura de protocolo **Whirlpool / Hisense / Kelon** (ampliamente documentado en la biblioteca `IRremoteESP8266` bajo el protocolo `Whirlpool` y soporte para la familia `DG11J1`):
- **Portadora**: 38 kHz.
- **Header**: Pulso activo (*Mark*) de ~8950 µs seguido de un espacio (*Space*) de ~4480 µs.
- **Bit 0**: Mark de ~590 µs + Space de ~530 µs.
- **Bit 1**: Mark de ~590 µs + Space de ~1650 µs.
- **Estructura**: Trama multifase en secciones con pausa intermedia (*gap* de ~7.9 ms).
- **Naturaleza HVAC**: Transmisión de estado absoluto (cada pulsación envía el conjunto completo: encendido, modo, temperatura, velocidad y deflectores).

---

## 4. Política de Versionado y Flujo de Trabajo (Git)
Para garantizar un historial de commits limpio, atómico y consistente:
1. **Contexto**: Antes de iniciar cualquier cambio o nueva etapa en el proyecto, ejecutar `git log -n 5` para revisar el formato y la convención de mensajes del repositorio.
2. **Confirmación**: Tras completar cada paso funcional y verificar su compilación/ejecución, ejecutar `git add .` (dentro del directorio del proyecto) y realizar el `git commit` descriptivo.

---

## 5. Hoja de Ruta y Fases de Desarrollo

### Fase 1: Prueba de Concepto Físico (Paso Inmediato)
**Objetivo**: Validar el circuito emisor/receptor y lograr el encendido del aire acondicionado.
- **Estrategia (Opción 1 - Sniff & Replay crudo con RMT)**:
  1. Utilizar el canal de recepción RMT (`rmt_new_rx_channel`) en **GPIO 5** para capturar los tiempos crudos al presionar "Power" en el control DG11J1-01.
  2. Imprimir los tiempos por monitor serial para verificar el disparo.
  3. Utilizar el canal de transmisión RMT (`rmt_new_tx_channel`) con portadora habilitada a 38 kHz en **GPIO 0** para reproducir la misma señal capturada apuntando al split.
  4. Confirmar la respuesta sonora (*beep*) y puesta en marcha del equipo.

### Fase 2: Decodificación Estructurada y Generador de Tramas
**Objetivo**: Controlar cualquier variable (temperatura, modo, fan) sin depender de grabaciones fijas.
- Extraer e implementar en C nativo para ESP-IDF la lógica de generación de tramas del protocolo DG11J1/Whirlpool.
- Proveer una función unificada `ac_send_command(power, mode, temp, fan, swing)`.

### Fase 3: Sincronización Horaria y Programación Local
**Objetivo**: Autonomía frente a cortes de red o reinicios.
- Conexión a red Wi-Fi y sincronización horaria mediante SNTP (`esp_netif_sntp`) con ajuste de zona horaria local (`TZ=ART3`).
- Programador de eventos locales (`esp_cron` o timers FreeRTOS) para disparar acciones a horas específicas.

### Fase 4: Servidor REST y Orquestación MQTT / Home Assistant
**Objetivo**: Integración con la Raspberry Pi 4 B y control domótico.
- **API REST Local**: Servidor HTTP embebido (`esp_http_server`) que reciba peticiones POST con payloads JSON para control directo y consulta de estado.
- **Cliente MQTT**: Conexión hacia el broker Mosquitto en la Raspberry Pi 4 B con soporte para **Home Assistant MQTT Auto-Discovery** (clase `climate`).

---

## 6. Enlaces y Recursos de Referencia
- **IRremoteESP8266 (Protocolo Whirlpool / DG11J1)**:
  - [ir_Whirlpool.h en GitHub](https://github.com/crankyoldgit/IRremoteESP8266/blob/master/src/ir_Whirlpool.h)
  - [ir_Whirlpool.cpp en GitHub](https://github.com/crankyoldgit/IRremoteESP8266/blob/master/src/ir_Whirlpool.cpp)
  - [Discusión y Decodificación DG11J1 (Issue #509)](https://github.com/crankyoldgit/IRremoteESP8266/issues/509)
- **Documentación Oficial ESP-IDF v5.5**:
  - [ESP-IDF v5.5 - Guía Periférico RMT](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32c3/api-reference/peripherals/rmt.html)
  - [Ejemplo oficial ir_nec_transceiver](https://github.com/espressif/esp-idf/tree/master/examples/peripherals/rmt/ir_nec_transceiver)
  - [ESP-IDF HTTP Server](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32c3/api-reference/protocols/esp_http_server.html)
  - [ESP-IDF MQTT Client](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32c3/api-reference/protocols/mqtt.html)
- **Integraciones y Programación**:
  - [Home Assistant MQTT Climate Integration](https://www.home-assistant.io/integrations/climate.mqtt/)
  - [Componente esp_cron en Espressif Registry](https://components.espressif.com/components/bubao/esp_cron)

---

## 7. Registro de Estado y Pruebas Realizadas

### 7.1 Hito Alcanzado: Fase 1 (PoC Físico) Superada con Éxito
- **Firmware probado**: Sniff & Replay con RMT en ESP32-C3 SuperMini.
- **Captura confirmada**: 172 símbolos RMT (~261 ms), cabecera Mark ~9100 µs / Space ~4000 µs.
- **Validación del actuador óptico**: El aire acondicionado Marshall respondió correctamente al comando retransmitido por el LED IR (GPIO 0 con portadora a 38 kHz y 50% duty cycle).
- **Conclusión de Hardware**: Tanto la etapa receptora (TSOP1738 en GPIO 5) como la etapa transmisora (Transistor NPN + LED IR en GPIO 0) están 100% validadas y operativas.

### 7.2 Siguiente Paso (Fase 2)
Una vez validada la capa física y la respuesta del equipo, el siguiente objetivo es implementar el **Generador de Tramas Estructurado** para el protocolo DG11J1 / Whirlpool. Esto permitirá fijar temperatura, cambiar modo (Frío/Calor/Ventilación), velocidad del ventilador y encendido/apagado por software de manera paramétrica sin depender de grabaciones fijas previas.

