#include <stdio.h>
#include "timer.h"

// Factor del prescaler que se quiere probar
#define FACTOR_PRUEBA 3

int main(void) {
  uint32_t tiempo_inicial;
  uint32_t tiempo_final;

  // 1. Seleccionar el factor del prescaler
  _io_ports[M6812_TMSK2] = FACTOR_PRUEBA;

  // 2. Inicializar el temporizador
  timer_init();
  serial_print("\r\n\nTemporizador inicializado.\n");

  serial_print("\r\nFactor TCM seleccionado: ");
  serial_print_dec_uint32(FACTOR_PRUEBA);

  // 3. Consultar el tiempo inicial
  serial_print("\r\n\nTiempo inicial:\n");
  serial_print("\r\nMicrosegundos: ");
  serial_print_dec_uint32(timer_get_us());

  // 4. Probar timer_sleep_ms
  serial_print("\r\n\nEsperando 1 segundo...\n");

  tiempo_inicial = timer_get_us();
  timer_sleep_ms(1000);
  tiempo_final = timer_get_us();

  // 5. Consultar el tiempo transcurrido
  serial_print("\r\nTiempo transcurrido en microsegundos: ");
  serial_print_dec_uint32(tiempo_final - tiempo_inicial);

  serial_print("\r\n\nPrueba de TCM_FACTOR finalizada.\n");

  // 6. Mantener el programa funcionando
  while (1) {
    // El temporizador sigue funcionando
  }
  return 0;
}
