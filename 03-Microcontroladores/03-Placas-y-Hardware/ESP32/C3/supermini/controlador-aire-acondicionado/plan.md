# Plan de Proyecto: Controlador Inteligente de Aire Acondicionado Marshall

## 1. Resumen Ejecutivo

Desarrollo de un firmware nativo en C sobre **ESP-IDF v5.5** para el microcontrolador **ESP32-C3 SuperMini**, diseñado para controlar de forma inteligente un acondicionador de aire marca **Marshall** (protocolo DG11J1-01) mediante señales infrarrojas (IR).

### Arquitectura de Integración
- **Microcontrolador (ESP32-C3 SuperMini)**: Ejecuta firmware 100% nativo en C (ESP-IDF v5.5). Toda la comunicación de red es asíncrona y orientada a eventos mediante el componente oficial **`esp-mqtt`**, prescindiendo de servidores HTTP/REST embebidos para optimizar memoria RAM y ciclos de CPU.
- **Servidor Domótico y Broker (Raspberry Pi 4 B)**:
  - **Eclipse Mosquitto**: Broker MQTT central para el transporte ágil y liviano de mensajes (desplegado en Docker).
  - **Home Assistant**: Centro de control domótico. Detecta automáticamente el dispositivo vía **MQTT Discovery** (`homeassistant/climate/<id>/config` con `retain=true`), generando la tarjeta nativa de clima (*climate card*) sin requerir YAML manual ni diseño de interfaces web ad-hoc.
  - **OpenClaw y Ollama en Docker**: Ya desplegados y en ejecución en contenedores sobre la Raspberry Pi 4 B. OpenClaw actúa como agente orquestador en lenguaje natural que interactuará con el ecosistema a través de Home Assistant y MQTT.
- **Sincronización Bidireccional en Tiempo Real**: El microcontrolador gestiona comandos remotos (`set`), disponibilidad mediante LWT (`availability`) y reporta cambios de estado consolidados (`status`), tanto si se ordenan por la red como si el usuario utiliza su control remoto físico DG11J1-01 (capturado por el demodulador TSOP1738 en GPIO 6).

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

- **GPIO 0**: Salida TX (Emisor LED Infrarrojo con transistor NPN y portadora de 38 kHz).
- **GPIO 6**: Entrada RX (Receptor demodulador TSOP1738 con pull-up interno).
- **GPIO 9**: Botón de usuario / BOOT integrado (disparo manual en placa).
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
  1. Utilizar el canal de recepción RMT (`rmt_new_rx_channel`) en **GPIO 6** para capturar los tiempos crudos al presionar "Power" en el control DG11J1-01.
  2. Imprimir los tiempos por monitor serial para verificar el disparo.
  3. Utilizar el canal de transmisión RMT (`rmt_new_tx_channel`) con portadora habilitada a 38 kHz en **GPIO 0** para reproducir la misma señal capturada apuntando al split.
  4. Confirmar la respuesta sonora (*beep*) y puesta en marcha del equipo.

### Fase 2: Decodificación Estructurada y Generador de Tramas (Completada)

**Objetivo**: Controlar cualquier variable (temperatura, modo, fan, swing) sin depender de grabaciones fijas.

- Extraer e implementar en C nativo para ESP-IDF la lógica de generación de tramas del protocolo DG11J1/Whirlpool (`dg11j1_protocol.h` y `dg11j1_protocol.c`).
- Proveer API unificada de codificación y decodificación de 21 bytes (168 bits) con cálculo de checksums XOR (`Sum1` y `Sum2`).
- Conversión precisa de tramas a símbolos de hardware RMT.
- Integración en la interfaz de consola interactiva del microcontrolador.

### Fase 3: Conectividad Wi-Fi Dinámica (Aprovisionamiento por BLE con NimBLE) y SNTP (Completada)

**Objetivo**: Conexión a red local sin credenciales hardcodeadas, configuración amigable desde celular mediante BLE y sincronización horaria autónoma.

- **Aprovisionamiento oficial por BLE (`wifi_provisioning` + NimBLE)**:
  - En lugar de SoftAP (que presentaba baja visibilidad en antenas de bajo perfil), se utiliza transporte **Bluetooth Low Energy (BLE)** compatible con la aplicación oficial **ESP BLE Provisioning** (Android / iOS).
  - Nombre del dispositivo BLE anunciado: `PROV_Aire_XXXX` (basado en la dirección MAC del chip).
  - Protocolo de seguridad: `Security 1` con código PoP (*Proof of Possession*) `12345678`.
  - El usuario escanea y selecciona su red Wi-Fi desde la interfaz del celular sin desconectarse de sus datos móviles.
  - Al completar la conexión y recibir IP, el stack BLE se **apaga y libera por completo la memoria RAM** asignada al controlador de radio Bluetooth (`WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM`).
- **Activación / Reseteo de Credenciales**:
  - Si el usuario **mantiene presionado el botón BOOT (GPIO 9)** durante >3 segundos o presiona `[b]` en la consola USB, el ESP reinicia el estado de aprovisionamiento en NVS y levanta el anuncio BLE.
- **Sincronización Horaria SNTP**:
  - Una vez en línea, sincroniza reloj con `esp_netif_sntp` ajustando zona horaria local (`TZ=ART3`).

### Fase 4: Comunicación MQTT Nativa, Home Assistant Discovery y Orquestación con OpenClaw

**Objetivo**: Integración domótica completa en red local con Home Assistant y OpenClaw corriendo sobre Raspberry Pi 4 B, gestionada íntegramente mediante MQTT nativo sobre ESP-IDF v5.5 con auto-descubrimiento.

- **Decisiones Fundamentales de Arquitectura**:
  - **Descarte de Dashboard Web ad-hoc y Servidor HTTP**: Se descartó la implementación de un servidor web local (`esp_http_server`) y el diseño de paneles web desde cero para maximizar la memoria RAM disponible, evitar sobrecarga de sockets TCP y eliminar vulnerabilidades web. Home Assistant proporciona de manera nativa la interfaz gráfica responsive y el control móvil.
  - **Descarte de Migración a ESPHome**: Se ratifica el firmware 100% nativo en C sobre **ESP-IDF v5.5** para preservar el control de bajo nivel del periférico RMT, la sincronización bidireccional por interrupciones en tiempo real y la mínima huella de memoria.
  - **Stack en Raspberry Pi 4 B**:
    - **Eclipse Mosquitto (Docker)**: Broker MQTT centralizado y liviano desplegado en contenedor (según guía `/home/agust/Downloads/gu_a_completa_de_eclipse_mosquitto.md`).
    - **Home Assistant**: Plataforma domótica encargada del estado global, automatizaciones, historial e interfaz gráfica.
    - **OpenClaw y Ollama (Docker)**: Ya en funcionamiento en contenedores. OpenClaw orquesta instrucciones en lenguaje natural para consultar estado, ajustar temperaturas y disparar automatizaciones a través de Home Assistant y MQTT.

- **Secuencia de Implementación de la Fase 4**:
  1. **Servidor (Raspberry Pi 4 B - Docker)**: Despliegue del contenedor `eclipse-mosquitto:2` con `mosquitto.conf` (`listener 1883`, `allow_anonymous true`) y puerto `1883:1883`. Validación de publicación/suscripción local.
  2. **Firmware (ESP32-C3 SuperMini - ESP-IDF v5.5)**: Desarrollo del módulo `mqtt_manager.h`/`.c` (`esp-mqtt`), configuración de la IP de la Raspberry Pi, LWT (`aire/living/availability`) y canales de telemetría.
  3. **Pruebas de Conectividad y Telemetría Bidireccional**: Monitoreo en el broker (`mosquitto_sub -t "aire/#" -v`) al flashear el ESP32 para verificar conexión, LWT `online`, envío de comandos (`set`) y recepción de estados del sniffer físico.
  4. **Home Assistant MQTT Discovery (`retain=true`)**: Publicación del payload de autoconfiguración en `homeassistant/climate/aire_marshall_living/config` para autogenerar la entidad `climate.aire_marshall_living` y su tarjeta gráfica sin tocar YAML.
  5. **Integración con OpenClaw**: Pruebas de control por lenguaje natural (ej. "Pon el aire en 24 grados en frío") orquestadas por OpenClaw hacia la entidad climática.

### Fase 5: Detalles, Pulido y Refinamiento (Feedback Visual por LED PWM con Fade)

**Objetivo**: Brindar feedback visual orgánico mediante el LED azul onboard (GPIO 2) usando modulación PWM (`ledc`) y efectos de desvanecimiento (*fading*):

- **Periférico LEDC (PWM en GPIO 2)**:
  - Generación de curvas suaves de brillo (difuminado/fading) en lugar de transiciones secas on/off.
- **Patrones de Estado Distintivos**:
  1. **Transmisión IR (TX)**: Ráfaga de parpadeos rápidos difuminados en secuencia mientras se emite al split.
  2. **Recepción IR (RX Sniffer)**: Doble pulso suave difuminado a velocidad media (confirmación visual de lectura del control físico).
  3. **Modo Configuración (SoftAP activo)**: Efecto "respiración" o parpadeo lento continuo (difuminado constante de 0 a 100% y descenso) indicando que el punto de acceso está abierto a la espera de credenciales.
  4. **Conexión Normal / Reposo**: LED apagado para no molestar de noche en la habitación.

---

## 6. Enlaces y Recursos de Referencia

- **IRremoteESP8266 (Protocolo Whirlpool / DG11J1)**:
  - [ir_Whirlpool.h en GitHub](https://github.com/crankyoldgit/IRremoteESP8266/blob/master/src/ir_Whirlpool.h)
  - [ir_Whirlpool.cpp en GitHub](https://github.com/crankyoldgit/IRremoteESP8266/blob/master/src/ir_Whirlpool.cpp)
  - [Discusión y Decodificación DG11J1 (Issue #509)](https://github.com/crankyoldgit/IRremoteESP8266/issues/509)
- **Documentación Oficial ESP-IDF v5.5**:
  - [ESP-IDF v5.5 - Guía Periférico RMT](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32c3/api-reference/peripherals/rmt.html)
  - [Ejemplo oficial ir_nec_transceiver](https://github.com/espressif/esp-idf/tree/master/examples/peripherals/rmt/ir_nec_transceiver)
  - [ESP-IDF MQTT Client (esp-mqtt)](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32c3/api-reference/protocols/mqtt.html)
- **Integraciones y Ecosistema Domótico**:
  - [Guía de Configuración e Instalación de Eclipse Mosquitto](file:///home/agust/Downloads/gu_a_completa_de_eclipse_mosquitto.md)
  - [Home Assistant MQTT Climate Integration](https://www.home-assistant.io/integrations/climate.mqtt/)
  - [Home Assistant MQTT Discovery Standard](https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery)
  - [Eclipse Mosquitto MQTT Broker](https://mosquitto.org/)

---

## 7. Registro de Estado y Pruebas Realizadas
 
### 7.1 Hito Alcanzado: Fase 1 (PoC Físico) Superada con Éxito
 
- **Firmware probado**: Sniff & Replay con RMT en ESP32-C3 SuperMini.
- **Captura confirmada**: 172 símbolos RMT (~261 ms), cabecera Mark ~9100 µs / Space ~4000 µs.
- **Validación del actuador óptico**: El aire acondicionado Marshall respondió correctamente al comando retransmitido por el LED IR (GPIO 0 con portadora a 38 kHz y 50% duty cycle).
- **Conclusión de Hardware**: Tanto la etapa receptora (TSOP1738) como la etapa transmisora (Transistor NPN + LED IR en GPIO 0) quedaron 100% validadas.
 
### 7.2 Hito Alcanzado: Fase 2 (Codificación y Decodificación Estructurada) Superada con Éxito
 
- **Pines finales validados**: Receptor TSOP1738 reubicado al **GPIO 6** (digital estándar, dejando libres los ADC GPIO 0-4), Emisor IR en **GPIO 0**, Botón **BOOT** en GPIO 9.
- **Módulo de Protocolo (`dg11j1_protocol.h` / `.c`)**:
-   - Soporte completo para tramas multifase de 21 bytes (168 bits) en 3 secciones.
-   - Cálculo exacto de checksums `Sum1` y `Sum2`.
-   - Generador dinámico de símbolos RMT (172 pulsos).
-   - Decodificador con tolerancia temporal y sincronización bidireccional.
-   - Corrección crítica de comportamiento: el bit de Power Toggle solo se conmuta al ejecutar encendido/apagado, permitiendo cambios de temperatura y modo sin apagar el equipo.
- - **Validación en Hardware Real**: Control paramétrico verificado exitosamente sobre el split Marshall (cambio de temperatura, modo, velocidad de fan y oscilación de swing respondiendo sin apagar la unidad).
 
### 7.3 Hito Alcanzado: Fase 3 (Aprovisionamiento BLE y Conexión Wi-Fi) Superada con Éxito

- **Transición y Arquitectura**:
  - Se implementó exitosamente el componente `wifi_provisioning` con transporte BLE bajo el stack ligero **NimBLE** (`PROV_Aire_F2CC`, PoP `12345678`).
  - Tabla de particiones personalizada (`partitions.csv`) con 1.9 MB asignados a la app de fábrica en flash de 4 MB.
- **Validación en Hardware Real (Celular -> ESP32-C3)**:
  - Dispositivo detectado y emparejado mediante la app oficial **ESP BLE Provisioning** en smartphone.
  - Recepción de credenciales y conexión inmediata al router doméstico (**SSID: `Personal-CE7-2.4GHz`**).
  - Señal óptima confirmada: **RSSI -33 dBm** en canal 11 con cifrado WPA2-PSK.
  - Retención de IP dinámica asignada (`wifi_manager_get_ip_str()`) y visualización en menú interactivo.
  - **Eficiencia de Memoria**: Al conectarse, `wifi_prov_mgr_deinit()` desactivó el controlador Bluetooth liberando toda la memoria RAM (`WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM`).
  - Persistencia total: credenciales guardadas en NVS, reconexión automática en reinicios o cortes de energía sin intervención manual.
  - **Optimización de Coexistencia y Potencia RF (C3 SuperMini)**:
    - Se resolvió el fallo de colisión transitoria durante aprovisionamiento BLE (`wifi:Coexist: Wi-Fi connect fail`) mediante `esp_coex_preference_set(ESP_COEX_PREFER_WIFI)`.
    - Se limitó la potencia máxima RF a 12 dBm (48) para prevenir caídas de tensión en el regulador onboard de 3.3V y evitar micro-reseteos del USB CDC/JTAG.
    - Se desactivó el modo modem sleep (`WIFI_PS_NONE`) y se añadió backoff de 2 segundos en reconexiones STA.

---

### 7.4 Hito Alcanzado: Sincronización Horaria SNTP (ART3) Superada con Éxito

- **Módulo de Tiempo (`time_manager.h` / `.c`)**:
  - Implementación de cliente SNTP nativo con servidores `pool.ntp.org` vía `esp_netif_sntp`.
  - Configuración automática de zona horaria de Argentina (`TZ=ART3` / UTC-3).
  - Callback síncrono `time_sync_notification_cb` que notifica la hora sincronizada en tiempo real.
  - Integración en el menú interactivo de consola mostrando hora real sincronizada (formato `HH:MM:SS`).

### 7.5 Hito Alcanzado: Blindaje de Arranque y Supresión de Ráfagas Espurias IR

- **Diagnóstico de Arranque**:
  - En el ESP32-C3, el pin GPIO 9 (botón BOOT) es un strapping pin que transiciona por nivel bajo durante la secuencia de reset/arranque por hardware.
  - La tarea de lectura de botón interpretaba el transitorio como una pulsación corta (<3s), disparando involuntariamente una transmisión IR completa al encender.
  - Adicionalmente, secuencias CRLF residuales en el monitor serial disparaban transmisiones debido al mapeo de `\r` / `\n` en la consola interactiva.
- **Solución y Validación**:
  - `button_task`: Espera de estabilización en nivel ALTO de GPIO 9 al boot y elevación de umbral de filtrado a $\ge 120\text{ ms}$.
  - `console_input_task`: Eliminación de saltos de línea y espacios; disparo de transmisión restringido exclusivamente a la tecla `'t'`/`'T'`.
  - Protección de cola con `CMD_NONE = 0` y forzado de niveles inactivos en bajo en el canal RMT (`init_level = 0`, `eot_level = 0`).
  - Verificado en hardware real: arranque 100% silencioso y sin destellos espurios, manteniendo operatividad total ante pulsaciones manuales intencionales.

---

### 7.6 Hito Alcanzado: Fase 4 (Cliente MQTT Nativo, HA Discovery y Telemetría Bidireccional) Superada con Éxito

- **Despliegue del Broker Mosquitto (Docker en Raspberry Pi 4 B - 192.168.0.188:1883)**:
  - Broker operativo y validado en contenedor `broker-mosquitto`.
- **Módulo Cliente MQTT Nativo (`mqtt_manager.h` / `.c`) en ESP-IDF v5.5**:
  - Implementación modular y orientada a eventos con `esp-mqtt` y serialización JSON con `cJSON`.
  - Canal LWT (`aire/living/availability`) configurado con estados `online` y `offline` retenidos (`retain=true`, QoS 1).
  - Suscripción automática a tópicos de control (`aire/living/set/#`) e inyección segura en la cola de acciones `s_action_queue` para garantizar que la transmisión RMT ocurra siempre de forma thread-safe en la tarea principal.
- **Home Assistant MQTT Discovery**:
  - Generación y publicación del payload JSON de auto-descubrimiento en `homeassistant/climate/aire_marshall_F2CC/config` (`retain=true`).
  - Home Assistant autodetecta el dispositivo y genera la tarjeta nativa de clima (`climate card`) con rangos 16-30 °C, modos de operación (`off`, `cool`, `heat`, `dry`, `fan_only`, `auto`), velocidad de ventilador (`auto`, `low`, `medium`, `high`) y oscilación (`on`, `off`), prescindiendo de configuración manual en YAML.
- **Sincronización Bidireccional en Tiempo Real**:
  - Comandos remotos recibidos por MQTT (`aire/living/set/temp`, `mode`, etc.) actualizan inmediatamente el split Marshall y publican el estado consolidado a `aire/living/status`.
  - Pulsaciones en el control físico DG11J1-01 (capturadas por el receptor TSOP1738 en GPIO 6) decodifican la trama y publican automáticamente el nuevo estado a Mosquitto / Home Assistant.
- **Validación en Vivo (Raspberry Pi <-> ESP32-C3 SuperMini)**:
  - Conexión verificada: `New client connected from 192.168.0.182 as marshall_ac_F2CC`.
  - Envío de comandos remotos de temperatura (24 °C -> 25 °C) y modo (`cool` -> `heat` -> `cool`) desde la Raspberry Pi reflejados inmediatamente en la telemetría del broker.

---

### 7.7 Hito Alcanzado: Fase 5 (Feedback Visual LED PWM con Fade y Switch Independiente en HA) Superada con Éxito

- **Driver LEDC de Desvanecimiento por Hardware (`led_indicator.h` / `.c`)**:
  - Control por modulación PWM a 5 kHz con resolución de 10 bits sobre el LED onboard azul del ESP32-C3 SuperMini (GPIO 8, lógica activa en nivel bajo con `flags.output_invert = 1`).
  - Animaciones no bloqueantes gestionadas por una tarea dedicada FreeRTOS (`led_indicator_task`):
    - **Transmisión IR (TX)**: Ráfaga rápida de 3 destellos difuminados (~35 ms subida/bajada).
    - **Recepción IR (RX Sniffer)**: Doble pulso suave difuminado (~100 ms subida/bajada).
    - **Aprovisionamiento BLE**: Efecto "respiración" continua (fade suave 0 a 100% y descenso en 700 ms).
    - **Confirmación Wi-Fi**: Pulso suave único (250 ms) al asociarse y obtener IP.
    - **Reposo Nocturno**: LED 100% apagado en estado normal para evitar molestias lumínicas.
- **Entidad Independiente de Encendido (`switch.aire_marshall_F2CC_power`) en Home Assistant**:
  - Se añadió la publicación de MQTT Discovery en `homeassistant/switch/aire_marshall_F2CC_power/config` (`retain=true`) vinculada al mismo dispositivo ("Controlador Split Marshall").
  - Permite encender y apagar el split directamente con un toggle switch sin tocar el selector de modos de la tarjeta de clima.
- **Protección de Parámetros en el Arranque del ESP32**:
  - Se eliminó la emisión ciega de estado por defecto (`24°C / Cool`) al conectar al broker MQTT.
  - Al reiniciar el ESP32, ya no se sobreescriben ni reinician los ajustes previos retenidos en Mosquitto y configurados desde Home Assistant.
- **Validación de Usuario**:
  - Se confirmó el correcto funcionamiento de toda la pila: conexión MQTT, tarjeta nativa de clima en Home Assistant, switch independiente de encendido/apagado y respuesta a comandos remotos.

---

### 7.8 Hito Alcanzado: Sincronización Bidireccional del Sensor Infrarrojo (TSOP1738 en GPIO 6) y Publicación MQTT

- **Blindaje del Receptor y Tolerancia a Glitches**:
  - Pull-up interno explícito configurado en **GPIO 6** (`gpio_set_pull_mode(IR_RX_GPIO, GPIO_PULLUP_ONLY)`).
  - Algoritmo de escaneo de cabecera en `dg11j1_decode_rmt_symbols`: busca la marca (~8950 µs) y el espacio (~4484 µs) dentro del tren de símbolos recibidos, descartando ruidos ópticos o transitorios previos sin perder la trama del control.
  - Cola de eventos RMT ampliada a 4 elementos y purga automática (`xQueueReset`) al reactivar el receptor para evitar la captura de rebotes ópticos de nuestras propias transmisiones.
- **Validación Estricta de Protocolo (Antirruido / Cero Falsos Positivos)**:
  - Verificación de la firma de 2 bytes característica de DG11J1 / Whirlpool (`0x83 0x06`).
  - Validación cruzada bitwise de los dos checksums XOR (`Sum1` en byte 13 y `Sum2` en byte 20).
- **Corrección Crítica de la Lógica de Power (Toggle vs Ajuste Operativo)**:
  - En el protocolo DG11J1, el bit 2 del byte 2 es *Power Toggle* y solo viaja en alto si se presionó la tecla de encendido/apagado.
  - Si el usuario presiona **Power** (`cmd == AC_CMD_POWER`), el estado de encendido conmuta con respecto al estado previo (`out_state->power = !prev_state->power`).
  - Si el usuario presiona cualquier ajuste operativo (**Temperatura**, **Modo**, **Velocidad de Fan**, **Swing**), el equipo se mantiene o activa en **ON**, evitando que ajustes de grados apaguen erróneamente la unidad en el ESP32.
- **Telemetría y Publicación MQTT Detallada**:
  - Implementación de `mqtt_manager_publish_remote_event`:
    - Publica el evento inmediato a `aire/living/remote` con el nombre del comando (`"command": "Set Temperatura"`), código numérico (`"cmd_code"`), estado resultante y la secuencia cruda completa de 21 bytes en hexadecimal (`"raw_hex": "83 06 00 90 ..."`).
    - Actualiza simultáneamente el estado retenido en `aire/living/status` (`retain=true`), reflejando al instante la nueva temperatura, modo, fan y swing en la tarjeta nativa de clima y el switch de Home Assistant.
  - Feedback visual sincronizado: doble pulso suave difuminado en el LED onboard (`LED_EVENT_IR_RX`) confirmando la recepción física.

---

### 7.9 Siguientes Pasos (Próximas Sesiones)

1. **Paso 1: Pruebas de Campo con el Control Físico DG11J1-01**:
   - Apuntar el control físico al receptor TSOP1738 (GPIO 6) y pulsar distintos botones (cambio de temperatura, modo, fan, swing, power).
   - Verificar en consola y mediante `mosquitto_sub -t "aire/#" -v` la llegada inmediata de la secuencia en `aire/living/remote` y el reflejo instantáneo en la interfaz de Home Assistant.

2. **Paso 2: Orquestación Conversacional con OpenClaw (Docker en Raspberry Pi 4 B)**:
   - Integrar o crear la skill en OpenClaw para interactuar con la entidad de Home Assistant o directamente vía MQTT mediante lenguaje natural (ej. *"OpenClaw, enciende el aire a 24 en frío"*).

