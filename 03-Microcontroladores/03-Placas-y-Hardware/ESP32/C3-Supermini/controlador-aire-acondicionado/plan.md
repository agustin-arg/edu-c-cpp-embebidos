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

### Fase 3: Conectividad Wi-Fi Dinámica (Aprovisionamiento por BLE con NimBLE) y SNTP

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

### Fase 4: Servidor REST Local y Orquestación MQTT / Home Assistant / OpenClaw

**Objetivo**: Integración total en red local con interfaz web responsive, API JSON para Claude/OpenClaw y entidad climática en Home Assistant vía Raspberry Pi 4 B.

- **Servidor Web y API REST Embebida (`esp_http_server`)**:
  - Interfaz web simple y moderna servida desde el ESP32 (accesible desde navegador de celular o PC en la IP local o mDNS `aire-marshall.local`).
  - Endpoints JSON:
    - `GET /api/status`: Devuelve el JSON con el estado actual del aire.
    - `POST /api/set`: Permite a scripts o agentes IA (Claude, OpenClaw) enviar comandos directos vía HTTP sin depender de broker.
- **Cliente MQTT Bidireccional (`esp-mqtt`)**:
  - Conexión al broker Mosquitto en la Raspberry Pi 4 B.
  - **Suscripción (`aire/living/set`)**: Recepción de órdenes desde Home Assistant o OpenClaw, actualizando estado interno y emitiendo trama IR.
  - **Publicación Reactiva (`aire/living/status`)**: Sincronización en tiempo real ante órdenes remotas o capturas de control físico (GPIO 6).
  - **Home Assistant MQTT Auto-Discovery**: Creación automática de la tarjeta `climate` en Home Assistant.

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
 
### 7.3 Hito Alcanzado: Fase 3 (Migración e Implementación de Aprovisionamiento BLE con NimBLE)

- **Transición de SoftAP a BLE**:
  - Se sustituyó el esquema de punto de acceso Wi-Fi autónomo por el componente oficial `wifi_provisioning` con transporte BLE (stack ligero **NimBLE**).
  - Se creó tabla de particiones personalizada (`partitions.csv`) reservando 1.9 MB para la app de fábrica y se habilitó flash de 4 MB.
  - Anuncio BLE verificado en el aire por escaneo RF con el identificador: **`PROV_Aire_F2CC`** (MAC `0C:4E:A0:66:F2:CE`).
  - Configurado esquema de autenticación `Security 1` con código de posesión (PoP): `12345678`.
  - Liberación de memoria comprobada: tras la conexión exitosa a Wi-Fi, la llamada a `wifi_prov_mgr_deinit()` apaga la radio BLE y libera toda la RAM interna (`WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM`).
  - Reseteo en caliente: pulsación del botón BOOT (>3 seg) o tecla `[b]` / `[w]` en consola borra credenciales en NVS y reactiva el anuncio BLE.

---

### 7.4 Siguiente Paso (Fase 4 / Fase 5)

1. Conectar a la red Wi-Fi local y enlazar con el broker **MQTT** (Mosquitto en Raspberry Pi 4) para exponer el equipo a **Home Assistant** (`climate.mqtt`).
2. Implementar los endpoints REST (`/api/status`, `/api/set`) para control por red local.
3. Sincronización horaria con **SNTP** (`esp_netif_sntp`).
4. Implementar feedback visual suave con LED PWM (`ledc`) en GPIO 2 (Fase 5).
