/* test_timer_retrasos.c -- Prueba de la librería de temporización

   Pide por la serial el factor de división del temporizador y los
   retrasos que se desean y comprueba:
     - la consulta del tiempo transcurrido
     - las esperas activas (timer_sleep_ms y timer_sleep_us)
     - un timeout y dos funciones periódicas funcionando simultáneamente

   Autores: Alba Hidalgo Martín, Xuan Sun, Juhi Kamal Chatani Mansukhani
*/

#include <types.h>
#include <sys/param.h>
#include <sys/ports.h>
#include <sys/interrupts.h>
#include <sys/sio.h>
#include <sys/locks.h>
#include "timer.h"

/* Mayor valor en milisegundos que, pasado a microsegundos, cabe en 32 bits */
#define MAX_MSEG 4294967UL

/* Banderín para indicar que el timeout se ha ejecutado */
volatile uint8_t timeout_ejecutado = 0;

/* Veces que se ha ejecutado cada función periódica */
volatile uint16_t cuenta_periodica_1 = 0;
volatile uint16_t cuenta_periodica_2 = 0;

/* Función que se ejecutará una sola vez */
void mensaje_timeout(void) {
  serial_print("\r\n  [timeout] ejecutado en el mseg ");
  serial_print_dec_uint32(timer_get_ms());
  timeout_ejecutado = 1;
}

/* Primera función periódica */
void mensaje_periodico_1(void) {
  cuenta_periodica_1++;
  serial_print("\r\n  [periodica 1] mseg ");
  serial_print_dec_uint32(timer_get_ms());
}

/* Segunda función periódica: además invierte el led de la placa */
void mensaje_periodico_2(void) {
  cuenta_periodica_2++;
  _io_ports[M6812_PORTG] ^= M6812B_PG7;
  serial_print("\r\n  [periodica 2] mseg ");
  serial_print_dec_uint32(timer_get_ms());
}

/* Muestra el mensaje y pide un número de milisegundos por la serial.
   Lo limita para que al pasarlo a microsegundos quepa en 32 bits */
uint32_t pide_mseg(const char *mensaje) {
  uint32_t mseg;

  serial_print(mensaje);
  mseg = serial_get_dec_uint32();
  if (mseg > MAX_MSEG) {
    mseg = MAX_MSEG;
    serial_print("\r\n  Valor demasiado grande, se usa ");
    serial_print_dec_uint32(mseg);
  }
  return mseg;
}

/* Saca por la serial el tiempo transcurrido desde timer_init */
void muestra_tiempo(void) {
  serial_print("\r\nTiempo transcurrido: ");
  serial_print_dec_uint32(timer_get_ticks());
  serial_print(" ticks, ");
  serial_print_dec_uint32(timer_get_us());
  serial_print(" usg, ");
  serial_print_dec_uint32(timer_get_ms());
  serial_print(" mseg");
}

int main(void) {
  uint8_t factor;
  uint8_t id_timeout;
  uint8_t id_periodica_1;
  uint8_t id_periodica_2;
  uint32_t mseg;
  uint32_t useg;
  uint32_t inicio;
  uint32_t useg_timeout;
  uint32_t useg_periodo_1;
  uint32_t useg_periodo_2;

  /* Deshabilitamos interrupciones durante la inicialización */
  lock();

  /* Led de la placa como salida */
  _io_ports[M6812_DDRG] |= M6812B_PG7;

  serial_init();
  serial_print("\r\n" __FILE__ " ==========\r\n");

  /* 1. Inicializar el temporizador con el factor que indique el usuario */
  do {
    serial_print("\r\nFactor de division del temporizador (0 a 7): ");
    factor = serial_get_dec_uint8();
  } while (factor > 7);
  timer_init(factor); /* Deja las interrupciones habilitadas */
  serial_print("\r\nTemporizador inicializado");

  while (1) {
    /* 2. Consultar el tiempo */
    muestra_tiempo();

    /* 3. Probar timer_sleep_ms */
    mseg = pide_mseg("\r\n\nMilisegundos de espera con timer_sleep_ms: ");
    serial_print("\r\nEsperando...");
    inicio = timer_get_us();
    timer_sleep_ms(mseg);
    useg = timer_get_us() - inicio;
    serial_print(" han pasado ");
    serial_print_dec_uint32(useg);
    serial_print(" usg");

    /* 4. Probar timer_sleep_us */
    serial_print("\r\n\nMicrosegundos de espera con timer_sleep_us: ");
    useg = serial_get_dec_uint32();
    serial_print("\r\nEsperando...");
    inicio = timer_get_us();
    timer_sleep_us(useg);
    useg = timer_get_us() - inicio;
    serial_print(" han pasado ");
    serial_print_dec_uint32(useg);
    serial_print(" usg");

    /* 5. Probar un timeout y dos periódicas simultáneamente */
    serial_print("\r\n\nSe programan a la vez un timeout y dos periodicas");
    serial_print("\r\n(con 0 no se programa esa funcion)");
    useg_timeout = 1000UL * pide_mseg("\r\nMilisegundos del timeout: ");
    useg_periodo_1 =
        1000UL * pide_mseg("\r\nMilisegundos del periodo de la periodica 1: ");
    useg_periodo_2 =
        1000UL * pide_mseg("\r\nMilisegundos del periodo de la periodica 2: ");

    timeout_ejecutado = 0;
    cuenta_periodica_1 = 0;
    cuenta_periodica_2 = 0;
    id_timeout = TIMER_ERROR;
    id_periodica_1 = TIMER_ERROR;
    id_periodica_2 = TIMER_ERROR;
    if (useg_timeout) {
      id_timeout = timer_set_timeout(useg_timeout, mensaje_timeout);
      if (id_timeout == TIMER_ERROR)
        serial_print("\r\nERROR: no se pudo programar el timeout");
    }
    if (useg_periodo_1) {
      id_periodica_1 = timer_set_periodic(useg_periodo_1, mensaje_periodico_1);
      if (id_periodica_1 == TIMER_ERROR)
        serial_print("\r\nERROR: no se pudo programar la periodica 1");
    }
    if (useg_periodo_2) {
      id_periodica_2 = timer_set_periodic(useg_periodo_2, mensaje_periodico_2);
      if (id_periodica_2 == TIMER_ERROR)
        serial_print("\r\nERROR: no se pudo programar la periodica 2");
    }

    serial_print("\r\nPulsa una tecla para detener las periodicas");
    serial_print(" (y cancelar el timeout si sigue pendiente)\r\n");

    /* El temporizador trabaja mediante interrupciones: aquí solo se
       espera a que se reciba un carácter por la serial */
    while (!serial_receive_pending());
    serial_get(); /* Leer (y descartar) el carácter recibido */

    timer_stop_periodic(id_periodica_1);
    timer_stop_periodic(id_periodica_2);
    if (useg_timeout && !timeout_ejecutado) {
      timer_cancel_timeout(id_timeout);
      serial_print("\r\nTimeout cancelado antes de ejecutarse");
    }

    serial_print("\r\nPeriodicas detenidas. Ejecuciones: ");
    serial_print_dec_uint16(cuenta_periodica_1);
    serial_print(" y ");
    serial_print_dec_uint16(cuenta_periodica_2);
    serial_print("\r\n\n---- Se repite la prueba ----");
  }
}
