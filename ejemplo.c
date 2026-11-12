
#include <stdio.h>
#include "timer.h"

/* Función que se ejecutará una sola vez */
void mensaje_timeout(void) {
    serial_print("\r\nSe ha ejecutado el timeout\n");
}

/* Función que se ejecutará periódicamente */
void mensaje_periodico(void) {
    serial_print("\r\nEvento periódico ejecutado\n");
}

int main(void) {
    /* 1. Inicializar el temporizador */
    timer_init();
    serial_print("\r\n\nTemporizador inicializado.\n");

    /* 2. Consultar el tiempo */
    serial_print("\r\n\nTiempo inicial:\n");
    serial_print("\r\nMicrosegundos: %lu\n");
    serial_print_dec_uint16((unsigned long)timer_get_us());
    serial_print("\r\nMilisegundos: %lu\n");
    serial_print_dec_uint16((unsigned long)timer_get_ms());

    /* 3. Probar timer_sleep */
    serial_print("\r\n\nEsperando 2 segundos...\n");
    timer_sleep_ms(2000);
    serial_print("\r\nHan pasado aproximadamente 2 segundos.\n");

    /* 4. Probar timer_set_timeout */
    serial_print("\r\n\nConfigurando timeout de 30 segundos...\n");
    timer_set_timeout(30000000, mensaje_timeout);

    /* 5. Probar timer_set_periodic */
    serial_print("\r\nConfigurando evento periodico de 5 segundo...\n");
    timer_set_periodic(5000000, mensaje_periodico);

    /* 6. Mantener el programa funcionando */
    while (1) {
        /* El temporizador trabaja mediante interrupciones */
    }

    return 0;
}
