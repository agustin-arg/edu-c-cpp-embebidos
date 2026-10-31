# Plan de Implementación: Fase 2 - Generador y Decodificador Estructurado del Protocolo IR DG11J1 (Whirlpool)

## 1. Descripción del Objetivo

El objetivo de esta fase es sustituir la retransmisión cruda en memoria RAM (PoC validado de la Fase 1) por un **módulo estructurado y paramétrico en C nativo para ESP-IDF v5.5** que permita:
1. **Generar tramas IR válidas** para cualquier combinación de estado: Encendido/Apagado, Modo (Frío, Calor, Fan, Dry, Auto), Temperatura (16–30 °C), Velocidad del ventilador (Auto, Baja, Media, Alta), y Swing (Oscilación).
2. **Decodificar tramas IR recibidas** por el sensor TSOP1738 (GPIO 5) para actualizar el estado del microcontrolador si el usuario acciona el control remoto físico **DG11J1-01**.
3. **Transmitir vía RMT** (GPIO 0 a 38 kHz) construyendo los símbolos RMT dinámicamente con los tiempos exactos del protocolo.

---

## 2. Especificación Técnica del Protocolo DG11J1 / Whirlpool

Basado en el análisis de señales del control DG11J1-01 y las especificaciones de `IRremoteESP8266`:

- **Frecuencia portadora**: 38 kHz, 50% duty cycle.
- **Tiempos de modulación**:
  - **Cabecera**: Mark = 8950 µs, Space = 4484 µs (solo en Sección 1).
  - **Bit 1**: Mark = 597 µs, Space = 1649 µs.
  - **Bit 0**: Mark = 597 µs, Space = 533 µs.
  - **Gap entre secciones**: Mark = 597 µs, Space = 7920 µs.
  - **Fin de trama**: Mark = 597 µs.
- **Estructura de la trama**: Total de **21 bytes** (168 bits), divididos en 3 secciones:
  - **Sección 1** (6 bytes, bytes 0 a 5): Header + bytes 0..5 + Gap.
  - **Sección 2** (8 bytes, bytes 6 a 13): Bytes 6..13 + Gap. Incluye **Checksum 1** en byte 13.
  - **Sección 3** (7 bytes, bytes 14 a 20): Bytes 14..20. Incluye **Checksum 2** en byte 20.
- **Checksums (XOR bitwise)**:
  - `Sum1` (Byte 13) = XOR de los bytes 2 a 12 (ambos inclusive).
  - `Sum2` (Byte 20) = XOR de los bytes 14 a 19 (ambos inclusive).
- **Orden de bits**: LSB primero (Least Significant Bit first).

---

## 3. Arquitectura Modular Propuesta

Para mantener el código ordenado y desacoplado, separamos la lógica en componentes limpios:

```
controlador-aire-acondicionado/
├── main/
│   ├── CMakeLists.txt
│   ├── dg11j1_protocol.h        <-- [NUEVO] Tipos de datos, enums, constantes y API pública
│   ├── dg11j1_protocol.c        <-- [NUEVO] Construcción de tramas, checksums, conversión a RMT y parser
│   └── controlador-aire-acondicionado.c <-- [MODIFICAR] Integración con RMT, consola interactiva y prueba
```

```mermaid
flowchart LR
    subgraph App ["Aplicación Principal"]
        CMD["Comandos Consola / Botón BOOT"]
        STATE["ac_state_t (Estado Actual)"]
    end
    
    subgraph Protocol ["Módulo dg11j1_protocol"]
        GEN["dg11j1_build_frame()"]
        ENC["dg11j1_state_to_rmt_symbols()"]
        DEC["dg11j1_decode_rmt_symbols()"]
    end
    
    subgraph Hardware ["Periférico RMT (ESP-IDF v5.5)"]
        TX["RMT TX (GPIO 0 - 38 kHz)"]
        RX["RMT RX (GPIO 5 - TSOP1738)"]
    end
    
    CMD -->|Fijar Temp/Modo/Power| STATE
    STATE --> GEN
    GEN --> ENC
    ENC -->|rmt_symbol_word_t[]| TX
    RX -->|Símbolos capturados| DEC
    DEC -->|Actualiza si es válido| STATE
```

---

## 4. Archivos a Crear y Modificar

### 4.1 [NUEVO] `main/dg11j1_protocol.h`
Define:
- `typedef enum { AC_MODE_COOL = 2, AC_MODE_HEAT = 0, AC_MODE_AUTO = 1, AC_MODE_DRY = 3, AC_MODE_FAN = 4 } ac_mode_t;`
- `typedef enum { AC_FAN_AUTO = 0, AC_FAN_HIGH = 1, AC_FAN_MED = 2, AC_FAN_LOW = 3 } ac_fan_t;`
- `typedef enum { AC_CMD_POWER = 0x01, AC_CMD_TEMP = 0x02, AC_CMD_MODE = 0x06, AC_CMD_FAN = 0x11, AC_CMD_SWING = 0x07 } ac_cmd_t;`
- `typedef struct { bool power; ac_mode_t mode; uint8_t temp_c; ac_fan_t fan; bool swing; bool light; } ac_state_t;`
- Prototipos:
  - `void dg11j1_default_state(ac_state_t *state);`
  - `size_t dg11j1_encode(const ac_state_t *state, ac_cmd_t cmd, rmt_symbol_word_t *out_symbols, size_t max_symbols);`
  - `bool dg11j1_decode(const rmt_symbol_word_t *symbols, size_t num_symbols, ac_state_t *out_state);`
  - `void dg11j1_dump_state(const ac_state_t *state);`

### 4.2 [NUEVO] `main/dg11j1_protocol.c`
Implementa:
- Constantes fijas de sincronización (bytes `0x83`, `0x06`, etc.).
- Cálculo exacto de los dos checksums XOR.
- Conversión bit-a-bit a `rmt_symbol_word_t` con `level0=1` (Mark 38 kHz) y `level1=0` (Space).
- Decodificación y validación de símbolos capturados contra las tolerancias de tiempo (±25%).

### 4.3 [MODIFICAR] `main/CMakeLists.txt`
Agregar `dg11j1_protocol.c` a las fuentes del componente:
```cmake
idf_component_register(SRCS "controlador-aire-acondicionado.c" "dg11j1_protocol.c"
                       INCLUDE_DIRS ".")
```

### 4.4 [MODIFICAR] `main/controlador-aire-acondicionado.c`
- Integrar la consola serial interactiva con un menú de comandos por teclado:
  - `+` / `-`: Subir o bajar temperatura (16 °C a 30 °C).
  - `m`: Cambiar modo (Frío -> Calor -> Ventilador -> Dry -> Auto).
  - `f`: Cambiar velocidad del ventilador (Auto -> Baja -> Media -> Alta).
  - `p`: Alternar Encendido / Apagado (Power toggle).
  - `s`: Alternar Swing.
  - `t`: Transmitir el estado actual al aire acondicionado Marshall.
  - Botón **BOOT** (GPIO 9): Envía el comando actual inmediatamente.
- Cuando el TSOP1738 reciba una señal del control remoto físico DG11J1-01:
  - El decodificador analizará la trama.
  - Si es válida y coincide el checksum, actualizará `ac_state_t` y mostrará en consola: `[SYNC CONTROL REMOTO] Nuevo estado detectado: Modo Frío, 24°C, Fan Alto...`

---

## 5. Plan de Verificación

### Paso 1: Compilación
- Compilar el proyecto con `idf.py build` asegurando cero advertencias o errores de enlace.

### Paso 2: Flasheo y Monitor
- Flashear a la placa `ESP32-C3 SuperMini` conectada en `/dev/ttyACM0` (o puerto detectado).
- Abrir consola serie (`idf.py monitor`).

### Paso 3: Validación Bidireccional
1. **Prueba de Transmisión Generada**:
   - Ajustar por teclado: Modo Frío, 24 °C, Fan Medio.
   - Presionar `t` o botón BOOT apuntando al split Marshall.
   - Comprobar respuesta sonora (*beep*) del equipo y confirmación del display del split en 24 °C.
2. **Prueba de Recepción y Decodificación**:
   - Apuntar el control físico **DG11J1-01** al TSOP1738 (GPIO 5) y presionar "Temp +" o "Modo".
   - Verificar que la terminal serial imprima la decodificación exitosa con los valores exactos mostrados en la pantalla LCD del control remoto.

### Paso 4: Commit en Git
- Conforme a la política del repositorio, una vez verificado el funcionamiento, realizar commit descriptivo documentando la finalización de la Fase 2.
