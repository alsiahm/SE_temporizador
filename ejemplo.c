#include <stdio.h>
#include "timer.h"

/*Tres funciones de timeout distintas para comprobar que
   cada una se ejecuta en su momento */
void timeout_a(void) {
    serial_print("\r\nTimeout A (3 s)\n");
}

void timeout_b(void) {
    serial_print("\r\nTimeout B (6 s)\n");
}

void timeout_c(void) {
    serial_print("\r\nTimeout C (9 s)\n");
}

/* Función que se ejecutará periódicamente */
void mensaje_periodico(void) {
    serial_print("\r\nEvento periodico ejecutado\n");
}

/*Muestra el canal asignado o un aviso si no había libres */
void mostrar_canal(const char *nombre, int8_t canal) {
    serial_print("\r\n");
    serial_print(nombre);
    if (canal < 0) {
        serial_print(": no hay canales libres\n");
    } else {
        serial_print(" -> canal OC");
        serial_print_dec_uint8((uint8_t)canal);
        serial_print("\n");
    }
}

int main(void) {
    int8_t canal_periodico;

    /* 1. Inicializar el temporizador */
    timer_init();
    serial_print("\r\n\nTemporizador inicializado.\n");

    /* 2. Consultar el tiempo */
    serial_print("\r\n\nTiempo inicial (ms): ");
    serial_print_dec_uint32(timer_get_ms());

    /* 3. Probar timer_sleep */
    serial_print("\r\n\nEsperando 2 segundos...\n");
    timer_sleep_ms(2000);
    serial_print("\r\nHan pasado aproximadamente 2 segundos.\n");

    /* 4. [CAMBIO 9] Varios timeouts a la vez, cada uno en su canal */
    mostrar_canal("Timeout A", timer_set_timeout(3000000, timeout_a));
    mostrar_canal("Timeout B", timer_set_timeout(6000000, timeout_b));
    mostrar_canal("Timeout C", timer_set_timeout(9000000, timeout_c));

    /* 5. Periódico de 2 s; se guarda su canal para poder pararlo */
    canal_periodico = timer_set_periodic(2000000, mensaje_periodico);
    mostrar_canal("Periodico", canal_periodico);

    serial_print("\r\nPulsa cualquier tecla para detener el evento periodico\n");

    /* 6. Mantener el programa funcionando */
    while (1) {
        /* 7. Si se recibe un caracter, parar el periódico */
        if (serial_receive_pending()) {
            serial_get(); /* Leer (y descartar) el caracter recibido */
            if (canal_periodico >= 0) {
                timer_stop((uint8_t)canal_periodico); /* [CAMBIO 9] */
                canal_periodico = -1;
                serial_print("\r\nEvento periodico detenido\n");
            }
        }
    }

    return 0;
}
