# Aprendizaje de C y Sistemas Embebidos (Microcontroladores)

Este repositorio contiene mis **prácticas personales, ejercicios teóricos y proyectos prácticos** orientados al aprendizaje del lenguaje de programación **C**, enfocándome en su aplicación directa a **sistemas embebidos y microcontroladores**.

---

## Referencias Bibliográficas Base

Gran parte de los contenidos teóricos, ejercitación de sintaxis y conceptos fundamentales de programación en C están estructurados siguiendo la hoja de ruta y actividades de dos libros de referencia:

1. > **Deitel, H. M., & Deitel, P. J.**  
   > *Cómo Programar en C, C++ y Java* (Edición en Español).  
   > Prentice Hall / Pearson Education.  
   > *(Ubicación: `01-C-Fundamentos/como-programar-C-CPP-java-Deitel/`)*

2. > **Marzal Varó, Andrés & Gracia Luengo, Isabel**  
   > *Introducción a la programación con C* (De Python a C).  
   > Universitat Jaume I.  
   > *(Ubicación: `01-C-Fundamentos/introduccion-programacion-C-Andres-e-Isabel/`)*

---

## Estructura del Repositorio y Metodología

El aprendizaje sigue un enfoque progresivo y modular:

1. **Fundamentos de C (`01-C-Fundamentos/`):**
   - **`0-instalar-configurar-C/`**: Guía y pruebas de configuración del entorno de compilación (GCC / MinGW / Linux).
   - **`como-programar-C-CPP-java-Deitel/`**: Cuestionarios, ejercicios y resolución de capítulos teóricos y prácticos del libro de Deitel (Capítulos 1 al 13).
   - **`introduccion-programacion-C-Andres-e-Isabel/`**: Ejercitación y estudio del enfoque de transición desde Python hacia C, punteros, estructuras y memoria dinámica.
2. **Microcontroladores y Sistemas Embebidos (`02-Microcontroladores/`):**
   - **`01-Fundamentos-Embebidos/`**: Conceptos agnósticos del hardware (manipulación directa de registros bit a bit, mapas de memoria, máquinas de estado finitas y RTOS).
   - **`02-Perifericos-y-Componentes/`**: Periféricos electrónicos básicos y protocolos de comunicación (GPIO, ADC, PWM, UART, I2C, SPI, RF).
   - **`03-Placas-y-Hardware/`**: Implementaciones prácticas en placas de desarrollo:
     - **ESP32:** ESP32-NodeMCU-32S, ESP32-C3 Supermini, ESP32-C6, ESP32-S3.
     - **Arduino:** AVR ATmega328P (Uno/Nano), ATmega2560 (Mega).
     - **STM32:** Arquitectura ARM Cortex-M (Blue Pill, STM32 Nucleo).
3. **Raspberry Pi y Linux Embebido (`03-Raspberry-Pi/`):**
   - Pruebas y desarrollo en entornos SBC y Linux embebido.
