#include <stdio.h>
#include "timer.h"

// Bandera para indicar que el timeout se ha ejecutado
volatile uint8_t timeout_flag = 0;

// Función que se ejecutará una sola vez
void mensaje_timeout(void) {
  timeout_flag = 1; // Indicar que el timeout se ha ejecutado
}

int main(void) {
  // 1. Inicializar el temporizador
  timer_init();
  serial_print("\r\n\nTemporizador inicializado.\n");

  // 2. Probar timer_set_timeout con un intervalo de 2 segundos
  serial_print("\r\n\nConfigurando timeout de 2 segundos...\n");
  timeout_flag = 0;
  timer_set_timeout(2000000, mensaje_timeout);

  while (!timeout_flag) {
    // Esperar a que se ejecute el timeout
  }

  serial_print("\r\nPrimer timeout ejecutado correctamente.\n");

  // 3. Probar timer_set_timeout con un intervalo de 100 milisegundos
  serial_print("\r\n\nConfigurando timeout de 100 milisegundos...\n");
  timeout_flag = 0;
  timer_set_timeout(100000, mensaje_timeout);

  while (!timeout_flag) {
    // Esperar a que se ejecute el timeout
  }

  serial_print("\r\nSegundo timeout ejecutado correctamente.\n");

  // 4. Finalizar las pruebas
  serial_print("\r\n\nTodas las pruebas de timeout han finalizado.\n");

  while (1) {
    // Mantener el programa funcionando
  }

  return 0;
}

