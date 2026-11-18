/* test_timer_retrasos.c -- Prueba de la librería de temporización

   Pide por la serial el factor de división del temporizador y los
   retrasos que se desean y comprueba:
     - la consulta del tiempo transcurrido
     - las esperas activas (timer_sleep_ms y timer_sleep_us)
     - un timeout y dos funciones periódicas funcionando simultáneamente
     - varios timeouts a la vez, y que no se acepten más que canales hay

   Autores: Alba Hidalgo Martín, Xuan Sun, Juhi Kamal Chatani Mansukhani
*/

#include <types.h>
#include <sys/param.h>
#include <sys/ports.h>
#include <sys/interrupts.h>
#include <sys/sio.h>
#include <sys/locks.h>
#include "timer.h"

/* Funciones que se programan: muestran el milisegundo en que se ejecutan */
void funcion_timeout(void) {
  serial_print("\r\n  [timeout] mseg ");
  serial_print_dec_uint32(timer_get_ms());
}

void funcion_periodica_1(void) {
  serial_print("\r\n  [periodica 1] mseg ");
  serial_print_dec_uint32(timer_get_ms());
}

void funcion_periodica_2(void) {
  serial_print("\r\n  [periodica 2] mseg ");
  serial_print_dec_uint32(timer_get_ms());
}

/* Muestra el mensaje y devuelve el número que se teclee */
uint32_t pide(const char *mensaje) {
  serial_print(mensaje);
  return serial_get_dec_uint32();
}

int main(void) {
  uint8_t factor, i;
  uint8_t id_timeout, id_periodica_1, id_periodica_2;
  uint8_t ids[TIMER_NUM_CANALES];
  uint32_t retrasos[TIMER_NUM_CANALES];
  uint32_t espera, inicio;

  serial_init();

  /* 1. Inicializar el temporizador */
  serial_print("\r\nFactor de division del temporizador (0 a 7): ");
  factor = serial_get_dec_uint8();
  while (factor > 7) {
    serial_print("\r\nFactor no valido, debe ser de 0 a 7: ");
    factor = serial_get_dec_uint8();
  }
  timer_init(factor); /* Deja las interrupciones habilitadas */

  while (1) {
    /* 2. Consultar el tiempo */
    serial_print("\r\n\nTiempo: ");
    serial_print_dec_uint32(timer_get_ticks());
    serial_print(" ticks, ");
    serial_print_dec_uint32(timer_get_us());
    serial_print(" usg, ");
    serial_print_dec_uint32(timer_get_ms());
    serial_print(" mseg");

    /* 3. Esperas activas */
    espera = pide("\r\n\nMilisegundos a esperar: ");
    inicio = timer_get_ms();
    timer_sleep_ms(espera);
    serial_print("\r\nHan pasado ");
    serial_print_dec_uint32(timer_get_ms() - inicio);
    serial_print(" mseg");

    espera = pide("\r\nMicrosegundos a esperar: ");
    inicio = timer_get_us();
    timer_sleep_us(espera);
    serial_print("\r\nHan pasado ");
    serial_print_dec_uint32(timer_get_us() - inicio);
    serial_print(" usg");

    /* 4. Un timeout y dos periódicas a la vez. Se piden todos los valores
       antes de programar nada, para que no empiecen a contar mientras
       se teclea */
    retrasos[0] = pide("\r\n\nMicrosegundos del timeout: ");
    retrasos[1] = pide("\r\nMicrosegundos del periodo 1: ");
    retrasos[2] = pide("\r\nMicrosegundos del periodo 2: ");
    espera = pide("\r\nMilisegundos a esperar: ");

    id_timeout = timer_set_timeout(retrasos[0], funcion_timeout);
    id_periodica_1 = timer_set_periodic(retrasos[1], funcion_periodica_1);
    id_periodica_2 = timer_set_periodic(retrasos[2], funcion_periodica_2);

    /* Mientras se espera, las funciones se ejecutan por interrupciones */
    timer_sleep_ms(espera);

    /* Si el timeout ya se ejecutó, cancelarlo no hace nada */
    timer_cancel_timeout(id_timeout);
    timer_stop_periodic(id_periodica_1);
    timer_stop_periodic(id_periodica_2);

    /* 5. Un timeout en cada canal */
    for (i = 0; i < TIMER_NUM_CANALES; i++)
      retrasos[i] = pide("\r\nMicrosegundos de un timeout: ");
    espera = pide("\r\nMilisegundos a esperar: ");

    for (i = 0; i < TIMER_NUM_CANALES; i++)
      ids[i] = timer_set_timeout(retrasos[i], funcion_timeout);

    /* Con todos los canales ocupados, uno más debe dar error */
    if (timer_set_timeout(retrasos[0], funcion_timeout) == TIMER_ERROR)
      serial_print("\r\nUn timeout de mas da TIMER_ERROR: correcto");

    timer_sleep_ms(espera);

    for (i = 0; i < TIMER_NUM_CANALES; i++)
      timer_cancel_timeout(ids[i]);
  }
}
