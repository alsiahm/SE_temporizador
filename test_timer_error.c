
#include <stdio.h>
#include "timer.h"

// Bandera para indicar que el timeout se ha ejecutado
volatile uint8_t timeout_flag = 0; 

// Función que se ejecutará una sola vez
void mensaje_timeout(void) {
    serial_print("\r\nSe ha ejecutado el timeout\n");
    timeout_flag = 1; // Indicar que el timeout se ha ejecutado
}

// Función que se ejecutará periódicamente 
void mensaje_periodico(void) {
    serial_print("\r\nEvento periódico ejecutado\n");
}


int main(void) {
    // 1. Inicializar el temporizador
    timer_init();
    serial_print("\r\n\nTemporizador inicializado.\n");

    // 2. Consultar el tiempo
    serial_print("\r\n\nTiempo inicial:\n");
    serial_print("\r\nMicrosegundos: \n");
    serial_print_dec_uint32(timer_get_us());
    serial_print("\r\nMilisegundos: \n");
    serial_print_dec_uint32(timer_get_ms());

    // 3. Probar timer_sleep
    serial_print("\r\n\nEsperando 10 segundos...\n");
    timer_sleep_ms(10000);
    serial_print("\r\nHan pasado aproximadamente 10 segundos.\n");

    // 4. Probar timer_set_timeout
    serial_print("\r\n\nConfigurando timeout de 20 segundos\n");
    timer_set_timeout(20000000, mensaje_timeout);
    while (!timeout_flag) {
        // Esperar a que se ejecute el timeout
    }

    // 5. Probar timer_set_periodic
    serial_print("\r\nConfigurando evento periodico de 5 segundos..\n");
    timer_set_periodic(5000000, mensaje_periodico);

    /* // 6. Mantener el programa funcionando
    while (1) {
        // El temporizador trabaja mediante interrupciones
    } */

    //6. Mantener el programa funcionando
    serial_print("\r\nPulsa cualquier tecla para detener el evento periodico\n");
    while (1) {
        /* El temporizador trabaja mediante interrupciones */
        /* 7. Si se recibe un caracter por el puerto serie, parar el periodico */
        if (serial_receive_pending()) {
            serial_get(); /* Leer (y descartar) el caracter recibido */
            timer_stop_periodic();
            serial_print("\r\nEvento periodico detenido\n");
        }
    }
    return 0;
}
