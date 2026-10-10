# Plan de Proyecto: Controlador Inteligente de Aire Acondicionado Marshall

## 1. Resumen Ejecutivo

Desarrollo de un firmware nativo en C sobre **ESP-IDF v5.5** para el microcontrolador **ESP32-C3 SuperMini**, diseñado para controlar de forma inteligente un acondicionador de aire marca **Marshall** (protocolo DG11J1-01) mediante señales infrarrojas (IR).

### Arquitectura de Integración
- **Microcontrolador (ESP32-C3 SuperMini)**: Ejecuta firmware 100% nativo en C (ESP-IDF v5.5). Toda la comunicación de red es asíncrona y orientada a eventos mediante el componente oficial **`esp-mqtt`**, prescindiendo de servidores HTTP/REST embebidos para optimizar memoria RAM y ciclos de CPU.
- **Servidor Domótico y Broker (Raspberry Pi 4 B)**:
  - **Eclipse Mosquitto**: Broker MQTT central para el transporte ágil y liviano de mensajes.
  - **Home Assistant**: Centro de control domótico. Detecta automáticamente el dispositivo vía **MQTT Discovery** (`homeassistant/climate/<id>/config` con `retain=true`), generando la tarjeta nativa de clima (*climate card*) sin requerir YAML manual ni diseño de interfaces web ad-hoc.
  - **OpenClaw en Docker**: Agente de inteligencia artificial orquestador que procesa instrucciones en lenguaje natural e interactúa con el ecosistema a través de Home Assistant y MQTT.
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
    - **Eclipse Mosquitto**: Broker MQTT centralizado y liviano.
    - **Home Assistant**: Plataforma domótica encargada del estado global, automatizaciones, historial e interfaz gráfica.
    - **OpenClaw en Docker**: Agente de inteligencia artificial orquestador que interpreta lenguaje natural para consultar estado, ajustar temperaturas y disparar automatizaciones a través de Home Assistant y MQTT.

- **Cliente MQTT Nativo (`esp-mqtt`)**:
  - Conexión persistente y eficiente al broker Mosquitto en la Raspberry Pi 4 B.
  - **Disponibilidad LWT (Last Will and Testament)**: Publicación en `aire/living/availability` (`online` / `offline`), permitiendo a Home Assistant y OpenClaw detectar caídas de enlace al instante.
  - **Suscripción a Comandos (`aire/living/set/...` o payload JSON)**: Recepción de directivas para modo (Cool, Heat, Dry, Fan, Auto, Off), temperatura deseada, velocidad de ventilador y swing, traduciéndolas de inmediato en tramas IR vía RMT hacia el split Marshall.
  - **Publicación Reactiva de Estados (`aire/living/status`)**: Reporte inmediato del estado HVAC consolidado ante comandos remotos y, fundamentalmente, ante capturas del demodulador TSOP1738 (GPIO 6) cuando se use el control remoto físico.

- **Home Assistant MQTT Discovery (`retain=true`)**:
  - Al conectarse al broker, el ESP32 publica el payload de autoconfiguración en `homeassistant/climate/aire_marshall_living/config` con la bandera `retain=true`.
  - Especifica los tópicos de estado, comando, disponibilidad y capacidades soportadas (`modes`, `fan_modes`, `swing_modes`, `temp_step=1`, `min_temp=16`, `max_temp=30`).
  - Home Assistant autogenera la entidad `climate.aire_marshall_living` y su tarjeta gráfica correspondiente (*climate card*) sin requerir código YAML manual ni configuración en el servidor.

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

### 7.6 Siguientes Pasos (Fase 4: MQTT Nativo, Home Assistant Discovery y Orquestación OpenClaw)

1. **Módulo Cliente MQTT Nativo (`esp-mqtt`)**:
   - Crear arquitectura modular `mqtt_manager.h` y `mqtt_manager.c` sobre ESP-IDF v5.5.
   - Configurar conexión al broker Mosquitto en la Raspberry Pi 4 B (URI `mqtt://<IP_PI>:1883`).
   - Implementar canal LWT (`aire/living/availability`) con estados `online` y `offline` retenidos.
2. **Generación y Publicación de Home Assistant MQTT Discovery**:
   - Construir el payload JSON de auto-descubrimiento en `homeassistant/climate/aire_marshall_living/config` con `retain=true`.
   - Mapear tópicos de estado y comando para temperatura (`temp`), modos (`mode`: `cool`, `heat`, `dry`, `fan_only`, `auto`, `off`), velocidades de ventilador (`fan_mode`: `auto`, `low`, `medium`, `high`) y oscilación (`swing_mode`: `on`, `off`).
3. **Manejo Bidireccional de Tópicos y Sincronización Reactiva**:
   - Suscripción y procesamiento de directivas entrantes (`aire/living/set/...`), actualizando el estado y emitiendo la ráfaga IR vía RMT al split Marshall.
   - Publicación reactiva del estado consolidado ante órdenes remotas y, en especial, tras la decodificación de señales del control remoto físico DG11J1-01 recibidas por el receptor TSOP1738 (GPIO 6).
4. **Validación en Raspberry Pi 4 B con OpenClaw en Docker**:
   - Comprobar la generación automática de la tarjeta nativa de clima (*climate card*) en Home Assistant sin escribir YAML.
   - Probar la orquestación y control por lenguaje natural a través del contenedor Docker de OpenClaw.
5. **Fase 5 (Refinamiento)**: Efectos visuales de LED PWM difuminado (`ledc` en GPIO 2).
