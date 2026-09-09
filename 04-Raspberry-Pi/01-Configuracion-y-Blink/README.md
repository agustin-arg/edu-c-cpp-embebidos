# Control de GPIO y parpadeo de LED en Raspberry Pi con C, GCC y WiringPi

Guía paso a paso para configurar, compilar y ejecutar un programa de parpadeo de LED en el pin GPIO 12 usando lenguaje C, el compilador GCC y la biblioteca WiringPi compilada desde la fuente.

---

### Paso 1: Conexión física del circuito

Conecta el circuito con la placa apagada o desconectada de la alimentación para evitar cortocircuitos accidentales.

* **Pin GPIO 12 (BCM 12)**: Corresponde al **pin físico 32** en la cabecera de 40 pines de la Raspberry Pi 4. Conecta este pin al ánodo del LED (terminal largo) en serie con una resistencia limitadora (entre 220 Ω y 330 Ω).
* **Masa (GND)**: Conecta el cátodo del LED (terminal corto) a cualquier pin GND, por ejemplo, el **pin físico 30** o **pin físico 34**.

> *Alerta de numeración de pines: En este proyecto se utiliza el modo de numeración **BCM** mediante la función `wiringPiSetupGpio()`. El número 12 hace referencia directa a la etiqueta de hardware **GPIO 12**, **NO** al pin físico 12 (que corresponde a GPIO 18). Conectar el LED al pin incorrecto provocará que el programa se ejecute sin mostrar cambios visuales.*

---

### Paso 2: Edición del código fuente en C

Abre tu archivo fuente con el editor Nano:

```bash
nano blink.c
```

* `nano`: Editor de texto basado en terminal para sistemas Linux.
* `blink.c`: Archivo de texto plano con código fuente en C que se abrirá para edición.

Reemplaza o escribe el siguiente código, configurado con el pin 12:

```c
#include <wiringPi.h>

// Definicion del pin GPIO 12 bajo numeracion BCM (Pin fisico 32)
#define LED 12 

int main(void) {
    // Inicializa la biblioteca utilizando la numeracion BCM
    wiringPiSetupGpio(); 
    
    // Configura el pin del LED como salida digital
    pinMode(LED, OUTPUT); 
    
    // Bucle infinito de alternancia logica
    for(;;) {
        digitalWrite(LED, HIGH); // Estado logico alto: 3.3V (enciende)
        delay(500);              // Pausa de 500 milisegundos
        digitalWrite(LED, LOW);  // Estado logico bajo: 0V (apaga)
        delay(500);              // Pausa de 500 milisegundos
    }
    
    return 0;
}
```

Guarda y cierra el archivo presionando `Ctrl + O`, `Enter` y luego `Ctrl + X`.

---

### Paso 3: Compilación del programa con GCC

Compila el archivo fuente enlazando dinámicamente la biblioteca de WiringPi instalada:

```bash
gcc blink.c -o blink -lwiringPi
```

* `gcc`: Compilador del proyecto GNU para C (*GNU Compiler Collection*).
* `blink.c`: Archivo de entrada con el código fuente en C.
* `-o`: Flag para designar el nombre del archivo binario ejecutable resultante.
* `blink`: Nombre del archivo ejecutable compilado.
* `-lwiringPi`: Flag del enlazador (*linker*) que vincula las funciones de la biblioteca `libwiringPi.so`.

> *Fallo de biblioteca compartida: Si al compilar o ejecutar el sistema reporta **"error while loading shared libraries: libwiringPi.so: cannot open shared object file"**, la caché de enlaces dinámicos no registró la ruta `/usr/local/lib`. Resuélvelo ejecutando `sudo ldconfig` antes de volver a intentar.*

---

### Paso 4: Ejecución del ejecutable generado

Inicia el programa desde la terminal:

```bash
sudo ./blink
```

* `sudo`: Ejecuta el comando con privilegios elevados de superusuario.
* `./blink`: Ruta relativa que ejecuta el binario `blink` presente en el directorio actual.

Para detener el parpadeo y salir del bucle infinito, presiona `Ctrl + C`.

> *Estado residual del pin tras interrupción: Al presionar **Ctrl + C**, el kernel termina el proceso de forma inmediata en la línea de ejecución donde se encuentre. Si el ciclo se interrumpe durante el estado **HIGH**, el GPIO permanecerá energizado y el LED quedará encendido. Si deseas apagarlo manualmente tras salir, puedes enviar un nivel bajo desde la consola con el comando `gpio -g write 12 0`.*
