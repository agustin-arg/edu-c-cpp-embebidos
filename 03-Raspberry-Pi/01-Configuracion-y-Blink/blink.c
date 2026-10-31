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
